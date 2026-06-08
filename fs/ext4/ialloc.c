/* 
 * Ext4 inode 分配器
 * 参考 Linux 内核 fs/ext4/ialloc.c（极简版）
 * 只支持单个块组，用于 MiniExt4 教学项目
 */

#include <linux/fs.h>
#include <linux/memory.h>
#include <fs/ext4/ext4.h>
#include <lib/printf.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 前向声明（块 I/O） */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern int ext4_write_block(uint32_t blocknr, const void *buf);
extern uint32_t ext4_get_block_size(void);

/* 简化的内存分配（使用静态池，避免依赖标准库） */
#define MAX_MALLOC_SIZE BLOCK_SIZE
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

static int ext4_read_group_desc(struct super_block *sb, uint32_t group,
				struct ext4_group_desc *out_gd)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t gd_base;
	uint32_t desc_off;
	uint32_t blk;
	uint32_t off;
	uint32_t copy_sz;
	char *buf;
	int ret;

	if (!sbi || !out_gd || sbi->s_desc_size == 0) {
		return -1;
	}
	if (group >= sbi->s_groups_count) {
		return -1;
	}

	gd_base = sbi->s_first_data_block + 1;
	if (block_size == 1024) {
		gd_base = 2;
	}
	desc_off = group * (uint32_t)sbi->s_desc_size;
	blk = gd_base + (desc_off / block_size);
	off = desc_off % block_size;
	copy_sz = (uint32_t)sbi->s_desc_size;
	if (copy_sz > sizeof(struct ext4_group_desc)) {
		copy_sz = sizeof(struct ext4_group_desc);
	}
	if (off + copy_sz > block_size) {
		return -1;
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}
	ret = ext4_read_block(blk, buf);
	if (ret < 0) {
		free(buf);
		return -1;
	}

	memset(out_gd, 0, sizeof(*out_gd));
	memcpy(out_gd, buf + off, copy_sz);
	free(buf);
	return 0;
}

static int ext4_write_group_desc(struct super_block *sb, uint32_t group,
				 const struct ext4_group_desc *gd)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t gd_base;
	uint32_t desc_off;
	uint32_t blk;
	uint32_t off;
	uint32_t copy_sz;
	char *buf;
	int ret;

	if (!sbi || !gd || sbi->s_desc_size == 0) {
		return -1;
	}
	if (group >= sbi->s_groups_count) {
		return -1;
	}

	gd_base = sbi->s_first_data_block + 1;
	if (block_size == 1024) {
		gd_base = 2;
	}
	desc_off = group * (uint32_t)sbi->s_desc_size;
	blk = gd_base + (desc_off / block_size);
	off = desc_off % block_size;
	copy_sz = (uint32_t)sbi->s_desc_size;
	if (copy_sz > sizeof(struct ext4_group_desc)) {
		copy_sz = sizeof(struct ext4_group_desc);
	}
	if (off + copy_sz > block_size) {
		return -1;
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}
	ret = ext4_read_block(blk, buf);
	if (ret < 0) {
		free(buf);
		return -1;
	}
	memcpy(buf + off, gd, copy_sz);
	ret = ext4_write_block(blk, buf);
	free(buf);
	return ret;
}

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
	struct ext4_group_desc gd_local;
	uint32_t bitmap_block;
	int ret;

	if (!sbi || !sbi->s_group_desc) {
		return 0;
	}
	if (group >= sbi->s_groups_count) {
		return 0;
	}
	if (ext4_read_group_desc(sb, group, &gd_local) < 0) {
		return 0;
	}
	bitmap_block = gd_local.bg_inode_bitmap_lo;
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

	if (!sbi || !sbi->s_group_desc) {
		return -1;
	}
	/* 只缓存了 group0 描述符，避免误把 group0 内容覆盖到其它组。 */
	if (group != 0) {
		return 0;
	}
	sbi->s_group_desc->bg_itable_unused_lo = 0;
	sbi->s_group_desc->bg_itable_unused_hi = 0;
	return ext4_write_group_desc(sb, 0, sbi->s_group_desc);
}

int ext4_write_group_desc_cached(struct super_block *sb, uint32_t group)
{
	return ext4_update_group_desc_inode(sb, group);
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
uint32_t ext4_new_inode_in_group(struct super_block *sb, uint32_t preferred_group)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t inodes_per_group;
	uint32_t groups_count;
	uint32_t start_group;
	uint32_t bitmap_block;
	char *bitmap_buf;
	uint32_t g;
	uint32_t groups_scanned;
	uint32_t new_ino = 0;
	struct ext4_group_desc gd_local;
	int ret;

	if (!sbi || !sbi->s_group_desc) {
		return 0;
	}

	inodes_per_group = sbi->s_inodes_per_group;
	if (inodes_per_group == 0) {
		return 0;
	}
	groups_count = sbi->s_groups_count;
	if (groups_count == 0) {
		groups_count = 1;
	}
	start_group = (preferred_group < groups_count) ? preferred_group : 0;

	bitmap_buf = (char *)malloc(block_size);
	if (!bitmap_buf) {
		return 0;
	}

	for (groups_scanned = 0; groups_scanned < groups_count; groups_scanned++) {
		uint32_t i;
		g = start_group + groups_scanned;
		if (g >= groups_count) {
			g -= groups_count;
		}

		bitmap_block = ext4_read_inode_bitmap(sb, g, bitmap_buf);
		if (bitmap_block == 0) {
			continue;
		}
		if (ext4_read_group_desc(sb, g, &gd_local) < 0) {
			continue;
		}

		for (i = 0; i < inodes_per_group; i++) {
			uint32_t byte = i / 8;
			uint32_t bit = i % 8;
			uint32_t cand_ino = g * inodes_per_group + i + 1;

			if (byte >= block_size) {
				break;
			}
			if (cand_ino == 0 || cand_ino > sbi->s_inodes_count) {
				break;
			}
			if (!(bitmap_buf[byte] & (1 << bit))) {
				bitmap_buf[byte] |= (1 << bit);
				new_ino = cand_ino;
				break;
			}
		}

		if (new_ino == 0) {
			continue;
		}

		ret = ext4_write_inode_bitmap(sb, bitmap_block, bitmap_buf);
		if (ret < 0) {
			new_ino = 0;
			continue;
		}

		if (gd_local.bg_free_inodes_count_lo > 0) {
			gd_local.bg_free_inodes_count_lo--;
		}
		gd_local.bg_itable_unused_lo = 0;
		gd_local.bg_itable_unused_hi = 0;
		ret = ext4_write_group_desc(sb, g, &gd_local);
		if (ret < 0) {
			new_ino = 0;
			continue;
		}
		if (g == 0) {
			*sbi->s_group_desc = gd_local;
		}

		(void)ext4_sync_super_free_counts(sb);
		break;
	}
	free(bitmap_buf);
	return new_ino;
}

uint32_t ext4_new_inode(struct super_block *sb)
{
	return ext4_new_inode_in_group(sb, 0);
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
	struct ext4_group_desc gd_local;
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

	if (group >= sbi->s_groups_count) {
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

	if (ext4_read_group_desc(sb, group, &gd_local) < 0) {
		free(bitmap_buf);
		return -1;
	}
	gd_local.bg_free_inodes_count_lo++;
	gd_local.bg_itable_unused_lo = 0;
	gd_local.bg_itable_unused_hi = 0;

	/* 写回组描述符 */
	ret = ext4_write_group_desc(sb, group, &gd_local);
	if (ret < 0) {
		free(bitmap_buf);
		return ret;
	}
	if (group == 0) {
		*sbi->s_group_desc = gd_local;
	}

	(void)ext4_sync_super_free_counts(sb);

	free(bitmap_buf);

	return ret;
}


