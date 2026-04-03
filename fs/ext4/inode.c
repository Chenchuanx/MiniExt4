/*
 * Ext4 inode 操作（目录与文件）
 * 参考 Linux 内核 fs/ext4/namei.c、inode.c
 */

#include <linux/fs.h>
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

	(void)excl;
	ino = ext4_new_inode(sb);
	if (ino == 0) {
		return -1; /* 无可用 inode */
	}
	inode = sb->s_op->alloc_inode(sb);
	if (!inode) {
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
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
	ei = (struct ext4_inode_info *)inode->i_private;
	if (ei) {
		memset(ei->i_block, 0, sizeof(ei->i_block));
		// ei->i_flags = EXT4_INODE_FLAG_EXTENTS;
	}
	sb->s_op->write_inode(inode, NULL);
	if (ext4_add_entry(dir, &dentry->d_name, ino) != 0) {
		sb->s_op->destroy_inode(inode);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
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
	uint32_t root_blocknr;	/* HTree 根块（包含 . / .. + 索引头） */
	uint32_t leaf_blocknr;	/* 第一个叶子数据块（实际目录项存放处） */
	char *buf;
	struct ext4_dir_entry *de;
	uint16_t rec1, rec2;
	int ret;

	ino = ext4_new_inode(sb);
	if (ino == 0) {
		return -1;
	}
	/* 为目录分配两个块：
	 * - root_blocknr：HTree 根块（包含 . / .. 与 HTree 索引头）
	 * - leaf_blocknr：首个叶子数据块（普通目录项放在这里）
	 *
	 * 目前实现为“单层 HTree”：所有非 . / .. 的目录项都放在 leaf_blocknr 块中。
	 */
	root_blocknr = ext4_new_block(sb);
	if (root_blocknr == 0) {
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	leaf_blocknr = ext4_new_block(sb);
	if (leaf_blocknr == 0) {
		ext4_free_block(sb, root_blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	inode = sb->s_op->alloc_inode(sb);
	if (!inode) {
		ext4_free_block(sb, leaf_blocknr);
		ext4_free_block(sb, root_blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	inode->i_ino = ino;
	inode->i_mode = S_IFDIR | (mode & 0777);
	/* HTree 目录占 2 个数据块：块 0 为 dx 根（. / .. + 索引），块 1 为叶子。
	 * i_size / i_blocks 必须覆盖全部目录字节，否则宿主 Linux 只映射第一块，
	 * 第二块上的目录项（文件、嵌套目录名）对内核不可见。 */
	inode->i_size = (uint64_t)block_size * 2;
	inode->i_blocks = (block_size / 512) * 2;
	inode->i_nlink = 2; /* . 和 .. */
	inode->i_atime = rtc_get_unix_time();
	inode->i_mtime = inode->i_atime;
	inode->i_ctime = inode->i_atime;
	inode->i_sb = sb;
	ei = (struct ext4_inode_info *)inode->i_private;
	if (ei) {
		memset(ei->i_block, 0, sizeof(ei->i_block));
		/* i_block[0] = HTree 根块，i_block[1] = 第一个叶子块 */
		ei->i_block[0] = root_blocknr;
		ei->i_block[1] = leaf_blocknr;
		/* 标记该目录使用 HTree 索引（与 Linux EXT4_INDEX_FL 一致） */
		ei->i_flags |= EXT4_INODE_FLAG_INDEX;
	}

	/* === 初始化 HTree 根块：包含 . 和 ..，以及 HTree 根信息 === */
	buf = (char *)malloc(block_size);
	if (!buf) {
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, leaf_blocknr);
		ext4_free_block(sb, root_blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
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

	/* 紧随其后的是一个“伪目录项头” + HTree 根信息 + 至少一个 dx_entry
	 *
	 * 伪目录项头用于让非 HTree 感知的代码在扫描目录块时，把整块后半部分
	 * 当作一个“空闲记录”（inode == 0），从而跳过 HTree 数据结构。 */
	{
		uint32_t off = rec1 + rec2;
		struct ext4_dir_entry *fake = (struct ext4_dir_entry *)((char *)buf + off);
		uint16_t fake_len = (uint16_t)(block_size - off);
		struct ext4_dx_root_info *info;
		struct ext4_dx_entry *entries;

		/* 伪目录项头：inode == 0，name_len == 0，file_type == 0 */
		fake->inode = 0;
		fake->rec_len = fake_len;
		fake->name_len = 0;
		fake->file_type = 0;

		/* 在伪目录项头之后布置 HTree 根信息与一个索引条目 */
		info = (struct ext4_dx_root_info *)((char *)fake + sizeof(struct ext4_dir_entry));
		info->reserved_zero = 0;
		info->hash_version = 0; /* 当前使用自定义 FNV 哈希，版本号先置 0 */
		info->info_length = (uint8_t)sizeof(struct ext4_dx_root_info);
		info->indirect_levels = 0; /* 单层 HTree，无额外 dx_node */
		info->unused_flags = 0;

		entries = (struct ext4_dx_entry *)(info + 1);
		/* 单层 HTree：仅一个叶子块，逻辑块号为 1，对应 i_block[1] */
		entries[0].hash = 0xFFFFFFFFU; /* 覆盖所有哈希范围 */
		entries[0].block = 1;	      /* 逻辑块号 1 -> i_block[1] */
	}

	ret = ext4_write_block(root_blocknr, buf);
	if (ret < 0) {
		free(buf);
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, leaf_blocknr);
		ext4_free_block(sb, root_blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}

	/* 初始化第一个叶子块：当前为空目录项区域（整个块一个空闲记录） */
	memset(buf, 0, block_size);
	de = (struct ext4_dir_entry *)buf;
	de->inode = 0;
	de->rec_len = (uint16_t)block_size;
	de->name_len = 0;
	de->file_type = 0;

	ret = ext4_write_block(leaf_blocknr, buf);
	free(buf);
	if (ret < 0) {
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, leaf_blocknr);
		ext4_free_block(sb, root_blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	sb->s_op->write_inode(inode, NULL);
	if (ext4_add_entry(dir, &dentry->d_name, ino) != 0) {
		sb->s_op->destroy_inode(inode);
		/* 失败时仅释放新目录自身的两个块与 inode；
		 * 父目录中未成功添加目录项，不需要额外回滚。 */
		ext4_free_block(sb, leaf_blocknr);
		ext4_free_block(sb, root_blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	d_instantiate(dentry, inode);
	/* 父目录 nlink 加 1 */
	dir->i_nlink++;
	if (sbi && dir->i_sb && dir->i_sb->s_op && dir->i_sb->s_op->write_inode) {
		dir->i_sb->s_op->write_inode(dir, NULL);
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
	inode->i_nlink--;
	sb->s_op->write_inode(inode, NULL);
	/* nlink 为 0 时可释放 inode 与块；简化实现：暂不回收 */
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
		return -1;
	}
	ext4_dir_foreach(inode, &n, count_non_dot_entries);
	if (n > 0) {
		return -1; /* 目录非空 */
	}
	if (ext4_remove_entry(dir, &dentry->d_name) != 0) {
		return -1;
	}
	inode->i_nlink -= 2; /* 原为 2（. 和 ..） */
	dir->i_nlink--;
	sb->s_op->write_inode(inode, NULL);
	sb->s_op->write_inode(dir, NULL);
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
