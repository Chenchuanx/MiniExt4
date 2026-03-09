/* 
 * Ext4 inode 分配器
 * 参考 Linux 内核 fs/ext4/ialloc.c（极简版）
 * 只支持单个块组，用于 MiniExt4 教学项目
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <lib/printf.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 前向声明（块 I/O） */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern int ext4_write_block(uint32_t blocknr, const void *buf);
extern uint32_t ext4_get_block_size(void);

/* 简化的内存操作函数（与其他 Ext4 文件保持一致） */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) {
		p[i] = (unsigned char)c;
	}
	return s;
}

static void *simple_memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	size_t i;
	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dest;
}

#define memset simple_memset
#define memcpy simple_memcpy

/* 简化的内存分配（使用静态池，避免依赖标准库） */
#define MAX_MALLOC_SIZE 4096
#define MAX_MALLOC_BLOCKS 8
static char malloc_pool[MAX_MALLOC_BLOCKS][MAX_MALLOC_SIZE];
static int malloc_used[MAX_MALLOC_BLOCKS];

static void *simple_malloc(size_t size)
{
	int i;
	if (size > MAX_MALLOC_SIZE) {
		return NULL;
	}
	for (i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (!malloc_used[i]) {
			malloc_used[i] = 1;
			return malloc_pool[i];
		}
	}
	return NULL;
}

static void simple_free(void *ptr)
{
	int i;
	if (!ptr)
		return;
	for (i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (malloc_pool[i] == ptr) {
			malloc_used[i] = 0;
			return;
		}
	}
}

#define malloc simple_malloc
#define free simple_free

/**
 * ext4_read_inode_bitmap - 读取 Inode 位图
 * @sb: 超级块
 * @group: 块组号
 * @bitmap_buf: 位图缓冲区（至少 block_size 字节）
 *
 * 返回位图块号，失败返回 0
 */
static uint32_t ext4_read_inode_bitmap(struct super_block *sb, uint32_t group,
				       char *bitmap_buf)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t bitmap_block;
	int ret;

	if (!sbi || !sbi->s_group_desc) {
		return 0;
	}

	/* 简化版：只支持第 0 块组 */
	if (group != 0) {
		return 0;
	}

	bitmap_block = sbi->s_group_desc->bg_inode_bitmap_lo;
	ret = ext4_read_block(bitmap_block, bitmap_buf);
	if (ret < 0) {
		return 0;
	}

	return bitmap_block;
}

/**
 * ext4_write_inode_bitmap - 写回 Inode 位图
 * @sb: 超级块
 * @bitmap_block: 位图块号
 * @bitmap_buf: 位图缓冲区
 *
 * 返回 0 表示成功，负数表示失败
 */
static int ext4_write_inode_bitmap(struct super_block *sb,
				   uint32_t bitmap_block,
				   char *bitmap_buf)
{
	(void)sb;
	if (!bitmap_buf || bitmap_block == 0) {
		return -1;
	}
	return ext4_write_block(bitmap_block, bitmap_buf);
}

/**
 * ext4_update_group_desc_inode - 更新组描述符中的 inode 相关字段
 * @sb: 超级块
 * @group: 块组号
 *
 * 将内存中的组描述符写回磁盘（与 fs/ext4/balloc.c 中的逻辑类似）
 */
static int ext4_update_group_desc_inode(struct super_block *sb, uint32_t group)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t group_desc_block;
	uint32_t block_size = ext4_get_block_size();
	char *buf;
	int ret;

	if (!sbi || !sbi->s_group_desc) {
		return -1;
	}

	/* 简化版：只支持第 0 块组 */
	if (group != 0) {
		return -1;
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}

	/* 计算组描述符所在块号（与 ext4_mkfs / ext4_fill_super 保持一致） */
	group_desc_block = sbi->s_first_data_block + 1;
	if (block_size == 1024) {
		group_desc_block = 2;
	}

	/* 读取原始组描述符块 */
	ret = ext4_read_block(group_desc_block, buf);
	if (ret < 0) {
		free(buf);
		return -1;
	}

	/* 用内存中的组描述符覆盖第一个描述符 */
	memcpy(buf, sbi->s_group_desc, sizeof(struct ext4_group_desc));

	/* 写回磁盘 */
	ret = ext4_write_block(group_desc_block, buf);
	free(buf);
	return ret;
}

/**
 * ext4_new_inode - 分配一个新的 Inode 号
 * @sb: 超级块
 *
 * 从 Inode 位图中找到第一个空闲 inode，标记为已使用，并更新组描述符。
 * 返回分配的 inode 号，失败返回 0。
 *
 * 注意：这里只返回 inode 号，不分配 VFS inode 结构体；
 *       VFS inode 由 ext4_alloc_inode() / vfs_alloc_inode() 管理。
 */
uint32_t ext4_new_inode(struct super_block *sb)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t inodes_per_group;
	uint32_t bitmap_block;
	char *bitmap_buf;
	uint32_t i;
	uint32_t new_ino = 0;
	int ret;

	if (!sbi || !sbi->s_group_desc) {
		return 0;
	}

	inodes_per_group = sbi->s_inodes_per_group;

	/* 检查是否有空闲 inode */
	if (sbi->s_group_desc->bg_free_inodes_count_lo == 0) {
		return 0;
	}

	/* 分配位图缓冲区 */
	bitmap_buf = (char *)malloc(block_size);
	if (!bitmap_buf) {
		return 0;
	}

	/* 读取 inode 位图（简化版：只支持第 0 块组） */
	bitmap_block = ext4_read_inode_bitmap(sb, 0, bitmap_buf);
	if (bitmap_block == 0) {
		free(bitmap_buf);
		return 0;
	}

	/* 在位图中查找第一个空闲 inode */
	/* 位图中：0 = 空闲，1 = 已使用 */
	for (i = 0; i < inodes_per_group; i++) {
		uint32_t byte = i / 8;
		uint32_t bit = i % 8;

		if (byte >= block_size) {
			break; /* 超出位图范围 */
		}

		if (!(bitmap_buf[byte] & (1 << bit))) {
			/* 找到空闲 inode，标记为已使用 */
			bitmap_buf[byte] |= (1 << bit);
			new_ino = i + 1; /* inode 号从 1 开始 */
			break;
		}
	}

	if (new_ino == 0) {
		/* 没有找到空闲 inode */
		free(bitmap_buf);
		return 0;
	}

	/* 写回 inode 位图 */
	ret = ext4_write_inode_bitmap(sb, bitmap_block, bitmap_buf);
	if (ret < 0) {
		free(bitmap_buf);
		return 0;
	}

	/* 更新组描述符中的空闲 inode 计数 */
	if (sbi->s_group_desc->bg_free_inodes_count_lo > 0) {
		sbi->s_group_desc->bg_free_inodes_count_lo--;
	}

	/* 写回组描述符 */
	ret = ext4_update_group_desc_inode(sb, 0);

	free(bitmap_buf);

	if (ret < 0) {
		return 0;
	}

	return new_ino;
}

/**
 * ext4_free_inode - 释放一个 Inode
 * @sb: 超级块
 * @ino: 要释放的 inode 号
 *
 * 将 inode 在位图中标记为空闲，并更新组描述符。
 * 返回 0 表示成功，负数表示失败。
 */
int ext4_free_inode(struct super_block *sb, uint32_t ino)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t inodes_per_group;
	uint32_t group;
	uint32_t index;
	uint32_t bitmap_block;
	char *bitmap_buf;
	int ret;

	if (!sbi || !sbi->s_group_desc) {
		return -1;
	}

	inodes_per_group = sbi->s_inodes_per_group;

	if (ino == 0) {
		return -1;
	}

	group = (ino - 1) / inodes_per_group;
	index = (ino - 1) % inodes_per_group;

	/* 简化版：只支持第 0 块组 */
	if (group != 0) {
		return -1;
	}

	if (index >= inodes_per_group) {
		return -1;
	}

	bitmap_buf = (char *)malloc(block_size);
	if (!bitmap_buf) {
		return -1;
	}

	/* 读取 inode 位图 */
	bitmap_block = ext4_read_inode_bitmap(sb, group, bitmap_buf);
	if (bitmap_block == 0) {
		free(bitmap_buf);
		return -1;
	}

	/* 操作位图 */
	{
		uint32_t byte = index / 8;
		uint32_t bit = index % 8;

		if (byte >= block_size) {
			free(bitmap_buf);
			return -1;
		}

		/* 如果已经是空闲的，直接返回成功 */
		if (!(bitmap_buf[byte] & (1 << bit))) {
			free(bitmap_buf);
			return 0;
		}

		/* 清除位，标记为空闲 */
		bitmap_buf[byte] &= ~(1 << bit);
	}

	/* 写回 inode 位图 */
	ret = ext4_write_inode_bitmap(sb, bitmap_block, bitmap_buf);
	if (ret < 0) {
		free(bitmap_buf);
		return -1;
	}

	/* 更新组描述符中的空闲 inode 计数 */
	sbi->s_group_desc->bg_free_inodes_count_lo++;

	/* 写回组描述符 */
	ret = ext4_update_group_desc_inode(sb, group);

	free(bitmap_buf);

	return ret;
}


