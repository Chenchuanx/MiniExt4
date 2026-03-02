/*
 * Ext4 文件与目录的 file_operations
 * 参考 Linux 内核 fs/ext4/file.c、dir.c
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <fs/dentry.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 前向声明 */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern int ext4_write_block(uint32_t blocknr, const void *buf);
extern uint32_t ext4_get_block_size(void);

/* 简化的内存操作函数 */
static void *simple_memcpy(void *d, const void *s, size_t n)
{
	unsigned char *dd = (unsigned char *)d;
	const unsigned char *ss = (const unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) dd[i] = ss[i];
	return d;
}
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) p[i] = (unsigned char)c;
	return s;
}
#define memcpy simple_memcpy
#define memset simple_memset

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

/* 普通文件操作 */

static int ext4_file_open(struct inode *inode, struct file *file)
{
	file->f_inode = inode;
	file->f_pos = 0;
	return 0;
}

static ssize_t ext4_file_read(struct file *file, char *buf, size_t count, loff_t *pos)
{
	struct inode *inode = file->f_inode;
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;
	uint32_t block_size = ext4_get_block_size();
	loff_t end;
	ssize_t read = 0;
	char *block_buf;

	if (!ei || !buf) return -1;
	end = *pos + (loff_t)count;
	if (end > inode->i_size) end = inode->i_size;
	if (*pos >= inode->i_size) return 0;
	count = (size_t)(end - *pos);
	if (count == 0) return 0;

	block_buf = (char *)malloc(block_size);
	if (!block_buf) return -1;

	while (count > 0) {
		uint32_t pos32 = (uint32_t)(*pos & 0xFFFFFFFFUL);
		uint32_t block_idx = pos32 / block_size;
		uint32_t off_in_block = pos32 % block_size;
		uint32_t blocknr;
		size_t to_copy;
		int ret;

		if (block_idx >= 12) break;
		blocknr = ei->i_block[block_idx];
		if (blocknr == 0) break;
		ret = ext4_read_block(blocknr, block_buf);
		if (ret < 0) { free(block_buf); return -1; }
		to_copy = block_size - off_in_block;
		if (to_copy > count) to_copy = count;
		memcpy(buf, block_buf + off_in_block, to_copy);
		read += (ssize_t)to_copy;
		buf += to_copy;
		*pos += (loff_t)to_copy;
		count -= to_copy;
	}

	free(block_buf);
	return read;
}

static ssize_t ext4_file_write(struct file *file, const char *buf, size_t count, loff_t *pos)
{
	struct inode *inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;
	uint32_t block_size = ext4_get_block_size();
	loff_t end_pos = *pos + (loff_t)count;
	ssize_t written = 0;
	char *block_buf;

	if (!ei || !buf) return -1;
	block_buf = (char *)malloc(block_size);
	if (!block_buf) return -1;

	while (count > 0) {
		uint32_t pos32 = (uint32_t)(*pos & 0xFFFFFFFFUL);
		uint32_t block_idx = pos32 / block_size;
		uint32_t off_in_block = pos32 % block_size;
		uint32_t blocknr;
		size_t to_copy;
		int ret;

		if (block_idx >= 12) break;
		blocknr = ei->i_block[block_idx];
		if (blocknr == 0) {
			blocknr = ext4_new_block(sb);
			if (blocknr == 0) break;
			ei->i_block[block_idx] = blocknr;
			memset(block_buf, 0, block_size);
		} else {
			ret = ext4_read_block(blocknr, block_buf);
			if (ret < 0) { free(block_buf); return -1; }
		}
		to_copy = block_size - off_in_block;
		if (to_copy > count) to_copy = count;
		memcpy(block_buf + off_in_block, buf, to_copy);
		ret = ext4_write_block(blocknr, block_buf);
		if (ret < 0) { free(block_buf); return -1; }
		written += (ssize_t)to_copy;
		buf += to_copy;
		*pos += (loff_t)to_copy;
		count -= to_copy;
	}
	if (end_pos > inode->i_size) {
		uint32_t end32 = (uint32_t)(end_pos & 0xFFFFFFFFUL);
		inode->i_size = end_pos;
		inode->i_blocks = (unsigned long)((end32 + 511) / 512);
		sb->s_op->write_inode(inode, NULL);
	}
	free(block_buf);
	return written;
}

static int ext4_file_release(struct inode *inode, struct file *file)
{
	(void)inode;
	(void)file;
	return 0;
}

/* Ext4 普通文件操作表 */
const struct file_operations ext4_file_operations = {
	ext4_file_read,
	ext4_file_write,
	ext4_file_open,
	ext4_file_release,
	NULL,  /* readdir */
	NULL,  /* llseek */
	NULL,  /* mmap */
};

/* 目录 readdir 相关 */

struct ext4_readdir_ctx {
	void *dirent;
	filldir_t filldir;
	loff_t offset;
};

static int ext4_readdir_fill(void *ctx, const char *name, int name_len,
			     unsigned long ino, unsigned int type)
{
	struct ext4_readdir_ctx *rctx = (struct ext4_readdir_ctx *)ctx;
	int r = rctx->filldir(rctx->dirent, name, name_len, rctx->offset, (u64)ino, type);
	rctx->offset++;
	return r != 0 ? 1 : 0;
}

static int ext4_dir_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *inode = file->f_inode;
	struct ext4_readdir_ctx ctx = {
		.dirent = dirent,
		.filldir = filldir,
		.offset = file->f_pos,
	};
	int r = ext4_dir_foreach(inode, &ctx, ext4_readdir_fill);
	return r < 0 ? -1 : 0;
}

static int ext4_dir_open(struct inode *inode, struct file *file)
{
	file->f_inode = inode;
	file->f_pos = 0;
	return 0;
}

/* Ext4 目录操作表 */
const struct file_operations ext4_dir_operations = {
	NULL,                 /* read */
	NULL,                 /* write */
	ext4_dir_open,
	ext4_file_release,    /* release */
	ext4_dir_readdir,
	NULL,                 /* llseek */
	NULL,                 /* mmap */
};
