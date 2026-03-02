/*
 * Ext4 inode 操作（目录与文件）
 * 参考 Linux 内核 fs/ext4/namei.c、inode.c
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <fs/dentry.h>
#include <lib/console.h>

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
#define BASE_TIME 0x5E0D9800

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
	inode->i_atime = BASE_TIME;
	inode->i_mtime = BASE_TIME;
	inode->i_ctime = BASE_TIME;
	inode->i_sb = sb;
	ei = (struct ext4_inode_info *)inode->i_private;
	if (ei) {
		memset(ei->i_block, 0, sizeof(ei->i_block));
		ei->i_flags = 0;
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
	uint32_t blocknr;
	char *buf;
	struct ext4_dir_entry *de;
	uint16_t rec1, rec2;
	int ret;

	ino = ext4_new_inode(sb);
	if (ino == 0) {
		return -1;
	}
	blocknr = ext4_new_block(sb);
	if (blocknr == 0) {
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	inode = sb->s_op->alloc_inode(sb);
	if (!inode) {
		ext4_free_block(sb, blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	inode->i_ino = ino;
	inode->i_mode = S_IFDIR | (mode & 0777);
	inode->i_size = block_size;
	inode->i_blocks = block_size / 512;
	inode->i_nlink = 2; /* . 和 .. */
	inode->i_atime = BASE_TIME;
	inode->i_mtime = BASE_TIME;
	inode->i_ctime = BASE_TIME;
	inode->i_sb = sb;
	ei = (struct ext4_inode_info *)inode->i_private;
	if (ei) {
		memset(ei->i_block, 0, sizeof(ei->i_block));
		ei->i_block[0] = blocknr;
		ei->i_flags = 0;
	}
	/* 写入目录块（含 . 和 ..） */
	buf = (char *)malloc(block_size);
	if (!buf) {
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	memset(buf, 0, block_size);
	de = (struct ext4_dir_entry *)buf;
	rec1 = (8 + 1 + 3) & ~3;
	de->inode = (uint32_t)ino;
	de->rec_len = rec1;
	de->name_len = 1;
	de->name[0] = '.';
	de = (struct ext4_dir_entry *)((char *)de + rec1);
	rec2 = (uint16_t)((block_size - rec1) & ~3);
	de->inode = dir->i_ino;
	de->rec_len = rec2;
	de->name_len = 2;
	de->name[0] = '.';
	de->name[1] = '.';
	ret = ext4_write_block(blocknr, buf);
	free(buf);
	if (ret < 0) {
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, blocknr);
		ext4_free_inode(sb, (uint32_t)ino);
		return -1;
	}
	sb->s_op->write_inode(inode, NULL);
	if (ext4_add_entry(dir, &dentry->d_name, ino) != 0) {
		sb->s_op->destroy_inode(inode);
		ext4_free_block(sb, blocknr);
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
