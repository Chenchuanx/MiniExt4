/*
 * Ext4 inode 操作（目录与文件）
 * 参考 Linux 内核 fs/ext4/namei.c、inode.c
 */

#include <linux/fs.h>
#include <linux/errno.h>
#include <fs/ext4/ext4.h>
#include <fs/dentry.h>
#include <lib/printf.h>
#include <drivers/rtc.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 前向声明 */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern int ext4_write_block(uint32_t blocknr, const void *buf);
extern uint32_t ext4_get_block_size(void);
extern int ext4_free_block(struct super_block *sb, uint32_t blocknr);
extern int ext4_free_inode(struct super_block *sb, uint32_t ino);

/* 简化的内存操作函数 */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) p[i] = (unsigned char)c;
	return s;
}
static void *simple_memcpy(void *d, const void *s, size_t n)
{
	unsigned char *dd = (unsigned char *)d;
	const unsigned char *ss = (const unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) dd[i] = ss[i];
	return d;
}
#define memset simple_memset
#define memcpy simple_memcpy

/* 简化的内存分配（使用静态池） */
#define MAX_MALLOC 4096
#define MAX_MALLOC_BLOCKS 4
static char malloc_pool[MAX_MALLOC_BLOCKS][MAX_MALLOC];
static int malloc_used[MAX_MALLOC_BLOCKS];
static void *simple_malloc(size_t size)
{
	int i;
	if (size > MAX_MALLOC) return NULL;
	for (i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (!malloc_used[i]) { malloc_used[i] = 1; return malloc_pool[i]; }
	}
	return NULL;
}
static void simple_free(void *p)
{
	int i;
	if (!p) return;
	for (i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (malloc_pool[i] == p) { malloc_used[i] = 0; return; }
	}
}
#define malloc simple_malloc
#define free simple_free

#define EXT4_INODE_SIZE 256

static uint32_t ext4_inode_group_of_ino(struct super_block *sb, uint32_t ino)
{
	struct ext4_sb_info *sbi;
	uint32_t inodes_per_group;

	if (!sb || ino == 0) {
		return 0;
	}
	sbi = (struct ext4_sb_info *)sb->s_fs_info;
	if (!sbi) {
		return 0;
	}
	inodes_per_group = sbi->s_inodes_per_group;
	if (inodes_per_group == 0) {
		return 0;
	}
	return (ino - 1) / inodes_per_group;
}

static uint64_t ext4_extent_pblock_from_ex(const struct ext4_extent *ex)
{
	return ((uint64_t)ex->ee_start_hi << 32) | (uint64_t)ex->ee_start_lo;
}

static uint64_t ext4_extent_child_pblock(const struct ext4_extent_idx *ix)
{
	return ((uint64_t)ix->ei_leaf_hi << 32) | (uint64_t)ix->ei_leaf_lo;
}

static void ext4_free_extent_tree_node(struct super_block *sb, struct ext4_extent_header *eh)
{
	uint32_t block_size;
	uint32_t i;

	if (!sb || !eh || eh->eh_magic != EXT4_EXT_MAGIC) {
		return;
	}

	block_size = ext4_get_block_size();
	if (eh->eh_depth == 0) {
		struct ext4_extent *ex = (struct ext4_extent *)(eh + 1);
		for (i = 0; i < eh->eh_entries; i++) {
			uint32_t len = ex[i].ee_len;
			uint64_t pblk = ext4_extent_pblock_from_ex(&ex[i]);
			uint32_t j;
			if (len == 0 || pblk == 0) {
				continue;
			}
			for (j = 0; j < len; j++) {
				(void)ext4_free_block(sb, (uint32_t)(pblk + j));
			}
		}
		return;
	}

	{
		struct ext4_extent_idx *idx = (struct ext4_extent_idx *)(eh + 1);
		for (i = 0; i < eh->eh_entries; i++) {
			uint64_t child_pblk = ext4_extent_child_pblock(&idx[i]);
			uint32_t child_block = (uint32_t)child_pblk;
			char *child_buf;
			struct ext4_extent_header *child_eh;

			if (child_block == 0) {
				continue;
			}

			child_buf = (char *)malloc(block_size);
			if (!child_buf) {
				/* 静态池不足时，至少确保把索引块本身回收，避免永久泄漏。 */
				(void)ext4_free_block(sb, child_block);
				continue;
			}
			if (ext4_read_block(child_block, child_buf) == 0) {
				child_eh = (struct ext4_extent_header *)child_buf;
				ext4_free_extent_tree_node(sb, child_eh);
			}
			free(child_buf);
			(void)ext4_free_block(sb, child_block);
		}
	}
}

static void ext4_free_legacy_data_blocks(struct inode *inode, struct ext4_inode_info *ei)
{
	struct super_block *sb;
	uint32_t block_size;
	uint32_t ptrs_per_block;
	uint32_t i;

	if (!inode || !ei) {
		return;
	}
	sb = inode->i_sb;
	if (!sb) {
		return;
	}
	block_size = ext4_get_block_size();
	ptrs_per_block = block_size / 4;

	/* 直接块 */
	for (i = 0; i < 12; i++) {
		if (ei->i_block[i] != 0) {
			(void)ext4_free_block(sb, ei->i_block[i]);
			ei->i_block[i] = 0;
		}
	}

	/* 单间接块 */
	if (ei->i_block[12] != 0) {
		uint32_t ind_blk = ei->i_block[12];
		uint32_t *ind_buf = (uint32_t *)malloc(block_size);
		if (ind_buf && ext4_read_block(ind_blk, ind_buf) == 0) {
			for (i = 0; i < ptrs_per_block; i++) {
				if (ind_buf[i] != 0) {
					(void)ext4_free_block(sb, ind_buf[i]);
				}
			}
		}
		if (ind_buf) {
			free(ind_buf);
		}
		(void)ext4_free_block(sb, ind_blk);
		ei->i_block[12] = 0;
	}

	/* 双重间接块 */
	if (ei->i_block[13] != 0) {
		uint32_t l1_blk = ei->i_block[13];
		uint32_t *l1_buf = (uint32_t *)malloc(block_size);
		if (l1_buf && ext4_read_block(l1_blk, l1_buf) == 0) {
			for (i = 0; i < ptrs_per_block; i++) {
				uint32_t l2_blk = l1_buf[i];
				uint32_t *l2_buf;
				uint32_t j;
				if (l2_blk == 0) {
					continue;
				}
				l2_buf = (uint32_t *)malloc(block_size);
				if (l2_buf && ext4_read_block(l2_blk, l2_buf) == 0) {
					for (j = 0; j < ptrs_per_block; j++) {
						if (l2_buf[j] != 0) {
							(void)ext4_free_block(sb, l2_buf[j]);
						}
					}
				}
				if (l2_buf) {
					free(l2_buf);
				}
				(void)ext4_free_block(sb, l2_blk);
			}
		}
		if (l1_buf) {
			free(l1_buf);
		}
		(void)ext4_free_block(sb, l1_blk);
		ei->i_block[13] = 0;
	}

	/* 当前实现未使用三重间接 i_block[14]，这里防御性释放。 */
	if (ei->i_block[14] != 0) {
		(void)ext4_free_block(sb, ei->i_block[14]);
		ei->i_block[14] = 0;
	}
}

void ext4_free_inode_data_blocks(struct inode *inode)
{
	struct ext4_inode_info *ei;
	struct ext4_extent_header *eh;

	if (!inode || !inode->i_sb) {
		return;
	}
	ei = (struct ext4_inode_info *)inode->i_private;
	if (!ei) {
		return;
	}

	if (S_ISREG(inode->i_mode) && (ei->i_flags & EXT4_INODE_FLAG_EXTENTS)) {
		eh = (struct ext4_extent_header *)ei->i_block;
		if (eh->eh_magic == EXT4_EXT_MAGIC) {
			ext4_free_extent_tree_node(inode->i_sb, eh);
		}
		memset(ei->i_block, 0, sizeof(ei->i_block));
	} else {
		ext4_free_legacy_data_blocks(inode, ei);
	}

	inode->i_size = 0;
	inode->i_blocks = 0;
}

/**
 * ext4_lookup - 按名称查找目录项
 */
static struct dentry *ext4_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	unsigned long ino;
	uint32_t blocknr, off;
	struct inode *inode;
	struct super_block *sb = dir->i_sb;

	(void)flags;
	if (ext4_find_entry(dir, &dentry->d_name, &ino, &blocknr, &off) != 0) {
		/* 未找到：返回负 dentry（d_inode 为 NULL） */
		return dentry;
	}
	inode = ext4_iget(sb, ino);
	if (!inode) {
		return dentry;
	}
	d_instantiate(dentry, inode);
	return dentry;
}

/**
 * ext4_create - 创建普通文件
 */
static int ext4_create(struct inode *dir, struct dentry *dentry, umode_t mode, int excl)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	unsigned long ino;
	struct ext4_inode_info *ei;
	uint32_t parent_group;

	(void)excl;
	parent_group = ext4_inode_group_of_ino(sb, (uint32_t)dir->i_ino);
	ino = ext4_new_inode_in_group(sb, parent_group);
	if (ino == 0) {
		/* inode 位图已无可用项（或分配失败），向上层明确返回无空间。 */
		return -ENOSPC;
	}
	inode = sb->s_op->alloc_inode(sb);
	if (!inode) {
		ext4_free_inode(sb, (uint32_t)ino);
		return -ENOMEM;
	}
	inode->i_ino = ino;
	inode->i_mode = S_IFREG | (mode & 0777);
	inode->i_size = 0;
	inode->i_blocks = 0;
	inode->i_nlink = 1;
	inode->i_atime = rtc_get_unix_time();
	inode->i_mtime = inode->i_atime;
	inode->i_ctime = inode->i_atime;
	inode->i_sb = sb;
	inode->i_op = &ext4_file_inode_operations;
	inode->i_fop = &ext4_file_operations;
	ei = (struct ext4_inode_info *)inode->i_private;
	if (ei) {
		struct ext4_extent_header *eh;
		memset(ei->i_block, 0, sizeof(ei->i_block));
		ei->i_alloc_group_hint = parent_group;
		/* 与 Linux ext4 对齐：普通文件默认使用 extents。 */
		ei->i_flags |= EXT4_INODE_FLAG_EXTENTS;
		eh = (struct ext4_extent_header *)ei->i_block;
		eh->eh_magic = (uint16_t)EXT4_EXT_MAGIC;
		eh->eh_entries = 0;
		eh->eh_depth = 0;
		eh->eh_generation = 0;
		eh->eh_max = (uint16_t)((sizeof(ei->i_block) - sizeof(*eh)) /
					sizeof(struct ext4_extent));
	}
	sb->s_op->write_inode(inode, NULL);
	if (ext4_add_entry(dir, &dentry->d_name, ino) != 0) {
		sb->s_op->destroy_inode(inode);
		ext4_free_inode(sb, (uint32_t)ino);
		/*
		 * 目录项插入失败常见于目录空间不足（当前实现未做目录扩展）；
		 * 返回 ENOSPC，避免被上层笼统映射成 EIO。
		 */
		return -ENOSPC;
	}
	d_instantiate(dentry, inode);
	return 0;
}

/**
 * ext4_mkdir - 创建目录
 */
static int ext4_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct super_block *sb = dir->i_sb;
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	struct inode *inode;
	unsigned long ino;
	struct ext4_inode_info *ei;
	uint32_t blocknr;
	char *buf;
	struct ext4_dir_entry *de;
	uint16_t rec1, rec2;
	int ret;
	uint32_t parent_group;

	parent_group = ext4_inode_group_of_ino(sb, (uint32_t)dir->i_ino);
	ino = ext4_new_inode_in_group(sb, parent_group);
	if (ino == 0) {
		return -ENOSPC;
	}
	/* 新目录默认只占 1 个块，保持与 Linux 常见行为一致（4KB 块时目录大小为 4KB）。 */
	blocknr = ext4_new_block_in_group(sb, parent_group);
	if (blocknr == 0) {
		ext4_free_inode(sb, (uint32_t)ino);
		return -ENOSPC;
	}
	inode = sb->s_op->alloc_inode(sb);
	if (!inode) {
		ext4_free_block(sb, blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -ENOMEM;
	}
	inode->i_ino = ino;
	inode->i_mode = S_IFDIR | (mode & 0777);
	inode->i_size = (uint64_t)block_size;
	inode->i_blocks = (block_size / 512);
	inode->i_nlink = 2; /* . 和 .. */
	inode->i_atime = rtc_get_unix_time();
	inode->i_mtime = inode->i_atime;
	inode->i_ctime = inode->i_atime;
	inode->i_sb = sb;
	inode->i_op = &ext4_dir_inode_operations;
	inode->i_fop = &ext4_dir_operations;
	ei = (struct ext4_inode_info *)inode->i_private;
	if (ei) {
		memset(ei->i_block, 0, sizeof(ei->i_block));
		ei->i_block[0] = blocknr;
		ei->i_alloc_group_hint = parent_group;
	}

	/* 初始化目录块：仅包含 "."、".."，其余空间作为空闲目录项。 */
	buf = (char *)malloc(block_size);
	if (!buf) {
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -ENOMEM;
	}
	memset(buf, 0, block_size);
	de = (struct ext4_dir_entry *)buf;

	/* "." 条目 */
	rec1 = (uint16_t)((8 + 1 + 3) & ~3);
	de->inode = (uint32_t)ino;
	de->rec_len = rec1;
	de->name_len = 1;
	de->file_type = 2; /* DT_DIR */
	de->name[0] = '.';

	/* ".." 条目 */
	de = (struct ext4_dir_entry *)((char *)de + rec1);
	rec2 = (uint16_t)((8 + 2 + 3) & ~3);
	de->inode = dir->i_ino;
	de->rec_len = rec2;
	de->name_len = 2;
	de->file_type = 2; /* DT_DIR */
	de->name[0] = '.';
	de->name[1] = '.';

	/* 剩余空间作为一个空闲目录项，供后续 ext4_add_entry 插入真实目录项。 */
	{
		uint32_t off = rec1 + rec2;
		struct ext4_dir_entry *fake = (struct ext4_dir_entry *)((char *)buf + off);
		fake->inode = 0;
		fake->rec_len = (uint16_t)(block_size - off);
		fake->name_len = 0;
		fake->file_type = 0;
	}

	ret = ext4_write_block(blocknr, buf);
	if (ret < 0) {
		free(buf);
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -EIO;
	}
	free(buf);
	sb->s_op->write_inode(inode, NULL);
	if (ext4_add_entry(dir, &dentry->d_name, ino) != 0) {
		sb->s_op->destroy_inode(inode);
		/* 失败时仅释放新目录自身的块与 inode；
		 * 父目录中未成功添加目录项，不需要额外回滚。 */
		ext4_free_block(sb, blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -ENOSPC;
	}
	d_instantiate(dentry, inode);
	/* 父目录 nlink 加 1 */
	dir->i_nlink++;
	if (sbi && dir->i_sb && dir->i_sb->s_op && dir->i_sb->s_op->write_inode) {
		dir->i_sb->s_op->write_inode(dir, NULL);
	}
	if (sbi && sbi->s_group_desc) {
		uint32_t g = ext4_inode_group_of_ino(sb, (uint32_t)ino);
		sbi->s_group_desc->bg_used_dirs_count_lo++;
		(void)ext4_write_group_desc_cached(sb, g);
	}
	return 0;
}

/**
 * ext4_unlink - 删除文件（取消链接）
 */
static int ext4_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dir->i_sb;

	if (!inode) {
		return -1;
	}
	if (ext4_remove_entry(dir, &dentry->d_name) != 0) {
		return -1;
	}

	/*
	 * 目录项已从磁盘目录中移除，需同步失效 dcache 项。
	 * 否则后续同名 lookup 可能命中“陈旧 dentry”，导致 touch/rm 循环异常。
	 */
	dentry->d_inode = (struct inode *)0;
	if (!list_empty(&dentry->d_child)) {
		list_del(&dentry->d_child);
		INIT_LIST_HEAD(&dentry->d_child);
	}

	inode->i_nlink--;
	if (inode->i_nlink == 0) {
		int wr;
		int fr;
		ext4_free_inode_data_blocks(inode);

		inode->i_mode = 0;
		inode->i_size = 0;
		inode->i_blocks = 0;
		wr = sb->s_op->write_inode(inode, NULL);
		fr = ext4_free_inode(sb, (uint32_t)inode->i_ino);
		if (wr < 0 || fr < 0) {
			return -EIO;
		}
		/* on-disk inode 已释放后，回收内存中的 VFS/ext4 inode 对象，避免池耗尽。 */
		sb->s_op->destroy_inode(inode);
	} else {
		if (sb->s_op->write_inode(inode, NULL) < 0) {
			return -EIO;
		}
	}
	return 0;
}

/* rmdir 用回调：统计非 . 和 .. 的目录项数量 */
static int count_non_dot_entries(void *ctx, const char *name, int name_len,
				 unsigned long ino, unsigned int type)
{
	int *n = (int *)ctx;
	(void)ino;
	(void)type;
	if (name_len == 1 && name[0] == '.') return 0;
	if (name_len == 2 && name[0] == '.' && name[1] == '.') return 0;
	(*n)++;
	return 0;
}

/**
 * ext4_rmdir - 删除目录（必须为空）
 */
static int ext4_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = dir->i_sb;
	int n = 0;

	if (!inode) {
		return -ENOENT;
	}
	ext4_dir_foreach(inode, &n, count_non_dot_entries);
	if (n > 0) {
		return -ENOTEMPTY; /* 目录非空 */
	}
	if (ext4_remove_entry(dir, &dentry->d_name) != 0) {
		return -ENOENT;
	}

	/* 与 unlink 一致：删除成功后失效 dcache 项，避免命中陈旧目录项。 */
	dentry->d_inode = (struct inode *)0;
	if (!list_empty(&dentry->d_child)) {
		list_del(&dentry->d_child);
		INIT_LIST_HEAD(&dentry->d_child);
	}

	inode->i_nlink -= 2; /* 原为 2（. 和 ..） */
	dir->i_nlink--;
	ext4_free_inode_data_blocks(inode);
	/* 同 unlink：先将 inode 内容落盘为 deleted，再清位图。 */
	inode->i_mode = 0;
	inode->i_size = 0;
	inode->i_blocks = 0;
	if (sb->s_op->write_inode(inode, NULL) < 0) {
		return -EIO;
	}
	if (ext4_free_inode(sb, (uint32_t)inode->i_ino) < 0) {
		return -EIO;
	}
	/* 与 unlink 一致：目录 inode 从磁盘删除后，回收内存对象。 */
	sb->s_op->destroy_inode(inode);
	sb->s_op->write_inode(dir, NULL);
	{
		struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
		uint32_t g = ext4_inode_group_of_ino(sb, (uint32_t)inode->i_ino);
		if (sbi && sbi->s_group_desc && sbi->s_group_desc->bg_used_dirs_count_lo > 0) {
			sbi->s_group_desc->bg_used_dirs_count_lo--;
			(void)ext4_write_group_desc_cached(sb, g);
		}
	}
	return 0;
}

/* Ext4 目录 inode 操作表 */
const struct inode_operations ext4_dir_inode_operations = {
	ext4_create,   /* create */
	ext4_lookup,   /* lookup */
	NULL,          /* link */
	ext4_unlink,   /* unlink */
	NULL,          /* symlink */
	ext4_mkdir,    /* mkdir */
	ext4_rmdir,    /* rmdir */
	NULL,          /* mknod */
	NULL,          /* rename */
	NULL,          /* getattr */
	NULL,          /* setattr */
	NULL,          /* get_link */
};

/* Ext4 普通文件 inode 操作表 */
const struct inode_operations ext4_file_inode_operations = {
	NULL,  /* create */
	NULL,  /* lookup */
	NULL,  /* link */
	NULL,  /* unlink */
	NULL,  /* symlink */
	NULL,  /* mkdir */
	NULL,  /* rmdir */
	NULL,  /* mknod */
	NULL,  /* rename */
	NULL,  /* getattr */
	NULL,  /* setattr */
	NULL,  /* get_link */
};
