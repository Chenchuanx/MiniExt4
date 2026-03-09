/* 
 * Ext4 块分配器
 * 参考 Linux 内核 fs/ext4/balloc.c
 * 实现块的分配和释放
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <lib/printf.h>

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

/* 简化的内存分配（使用静态池） */
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
	if (!ptr) return;
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
 * ext4_read_block_bitmap - 读取块位图
 * @sb: 超级块
 * @group: 块组号
 * @bitmap_buf: 位图缓冲区（必须至少 block_size 字节）
 * 
 * 返回位图块号，失败返回 0
 */
static uint32_t ext4_read_block_bitmap(struct super_block *sb, uint32_t group,
				       char *bitmap_buf)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t bitmap_block;
	int ret;
	
	if (!sbi || !sbi->s_group_desc) {
		return 0;
	}
	
	/* 简化版：只处理第一个块组 */
	if (group != 0) {
		return 0;
	}
	
	/* 获取位图块号 */
	bitmap_block = sbi->s_group_desc->bg_block_bitmap_lo;
	
	/* 读取位图块 */
	ret = ext4_read_block(bitmap_block, bitmap_buf);
	if (ret < 0) {
		return 0;
	}
	
	return bitmap_block;
}

/**
 * ext4_write_block_bitmap - 写回块位图
 * @sb: 超级块
 * @bitmap_block: 位图块号
 * @bitmap_buf: 位图缓冲区
 * 
 * 返回 0 表示成功，负数表示失败
 */
static int ext4_write_block_bitmap(struct super_block *sb, uint32_t bitmap_block,
				   char *bitmap_buf)
{
	int ret;
	
	if (!bitmap_buf || bitmap_block == 0) {
		return -1;
	}
	
	ret = ext4_write_block(bitmap_block, bitmap_buf);
	return ret;
}

/**
 * ext4_update_group_desc - 更新组描述符
 * @sb: 超级块
 * @group: 块组号
 * 
 * 将内存中的组描述符写回磁盘
 */
static int ext4_update_group_desc(struct super_block *sb, uint32_t group)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t group_desc_block;
	uint32_t block_size = ext4_get_block_size();
	char *buf;
	int ret;
	
	if (!sbi || !sbi->s_group_desc) {
		return -1;
	}
	
	/* 简化版：只处理第一个块组 */
	if (group != 0) {
		return -1;
	}
	
	/* 分配缓冲区 */
	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}
	
	/* 计算组描述符块号 */
	group_desc_block = sbi->s_first_data_block + 1;
	if (block_size == 1024) {
		group_desc_block = 2;
	}
	
	/* 读取组描述符块 */
	ret = ext4_read_block(group_desc_block, buf);
	if (ret < 0) {
		free(buf);
		return -1;
	}
	
	/* 更新组描述符 */
	memcpy(buf, sbi->s_group_desc, sizeof(struct ext4_group_desc));
	
	/* 写回磁盘 */
	ret = ext4_write_block(group_desc_block, buf);
	
	free(buf);
	return ret;
}

/**
 * ext4_new_block - 分配一个新块
 * @sb: 超级块
 * 
 * 从块位图中找到第一个空闲块，标记为已使用
 * 返回分配的块号，失败返回 0
 */
uint32_t ext4_new_block(struct super_block *sb)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t blocks_per_group = sbi->s_blocks_per_group;
	uint32_t bitmap_block;
	uint32_t new_block = 0;
	char *bitmap_buf;
	uint32_t i;
	int ret;
	
	if (!sbi || !sbi->s_group_desc) {
		return 0;
	}
	
	/* 检查是否有空闲块 */
	if (sbi->s_group_desc->bg_free_blocks_count_lo == 0) {
		return 0;  /* 没有空闲块 */
	}
	
	/* 分配位图缓冲区 */
	bitmap_buf = (char *)malloc(block_size);
	if (!bitmap_buf) {
		return 0;
	}
	
	/* 读取块位图（简化版：只处理第一个块组） */
	bitmap_block = ext4_read_block_bitmap(sb, 0, bitmap_buf);
	if (bitmap_block == 0) {
		free(bitmap_buf);
		return 0;
	}
	
	/* 在位图中查找第一个空闲块 */
	/* 位图中：0 = 空闲，1 = 已使用 */
	/* 注意：块 0 通常是超级块，不应该被分配，从块 1 开始查找 */
	for (i = 1; i < blocks_per_group; i++) {
		uint32_t byte = i / 8;
		uint32_t bit = i % 8;
		
		if (byte >= block_size) {
			break;  /* 超出位图范围 */
		}
		
		/* 检查位是否为 0（空闲） */
		if (!(bitmap_buf[byte] & (1 << bit))) {
			/* 找到空闲块，标记为已使用 */
			bitmap_buf[byte] |= (1 << bit);
			new_block = i;
			break;
		}
	}
	
	if (new_block == 0) {
		/* 没有找到空闲块 */
		free(bitmap_buf);
		return 0;
	}
	
	/* 写回位图 */
	ret = ext4_write_block_bitmap(sb, bitmap_block, bitmap_buf);
	if (ret < 0) {
		free(bitmap_buf);
		return 0;
	}
	
	/* 更新组描述符中的空闲块计数 */
	if (sbi->s_group_desc->bg_free_blocks_count_lo > 0) {
		sbi->s_group_desc->bg_free_blocks_count_lo--;
	}
	
	/* 写回组描述符 */
	ret = ext4_update_group_desc(sb, 0);
	
	free(bitmap_buf);
	
	if (ret < 0) {
		return 0;
	}
	
	return new_block;
}

/**
 * ext4_free_block - 释放一个块
 * @sb: 超级块
 * @blocknr: 要释放的块号
 * 
 * 将块在位图中标记为空闲，更新组描述符
 * 返回 0 表示成功，负数表示失败
 */
int ext4_free_block(struct super_block *sb, uint32_t blocknr)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t blocks_per_group = sbi->s_blocks_per_group;
	uint32_t group = blocknr / blocks_per_group;
	uint32_t block_in_group = blocknr % blocks_per_group;
	uint32_t bitmap_block;
	char *bitmap_buf;
	int ret;
	
	if (!sbi || !sbi->s_group_desc) {
		return -1;
	}
	
	/* 简化版：只处理第一个块组 */
	if (group != 0) {
		return -1;
	}
	
	/* 检查块号是否有效 */
	if (block_in_group >= blocks_per_group) {
		return -1;
	}
	
	/* 分配位图缓冲区 */
	bitmap_buf = (char *)malloc(block_size);
	if (!bitmap_buf) {
		return -1;
	}
	
	/* 读取块位图 */
	bitmap_block = ext4_read_block_bitmap(sb, group, bitmap_buf);
	if (bitmap_block == 0) {
		free(bitmap_buf);
		return -1;
	}
	
	/* 检查块是否已使用 */
	{
		uint32_t byte = block_in_group / 8;
		uint32_t bit = block_in_group % 8;
		
		if (byte >= block_size) {
			free(bitmap_buf);
			return -1;
		}
		
		/* 如果块已经是空闲的，直接返回成功 */
		if (!(bitmap_buf[byte] & (1 << bit))) {
			free(bitmap_buf);
			return 0;
		}
		
		/* 标记块为空闲 */
		bitmap_buf[byte] &= ~(1 << bit);
	}
	
	/* 写回位图 */
	ret = ext4_write_block_bitmap(sb, bitmap_block, bitmap_buf);
	if (ret < 0) {
		free(bitmap_buf);
		return -1;
	}
	
	/* 更新组描述符中的空闲块计数 */
	sbi->s_group_desc->bg_free_blocks_count_lo++;
	
	/* 写回组描述符 */
	ret = ext4_update_group_desc(sb, group);
	
	free(bitmap_buf);
	
	return ret;
}

