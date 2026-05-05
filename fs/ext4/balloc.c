/* 
 * Ext4 块分配器
 * 参考 Linux 内核 fs/ext4/balloc.c
 * 实现块的分配和释放
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <lib/printf.h>
#include <drivers/ata.h>

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


static int ext4_read_group_desc(struct super_block *sb, uint32_t group,
				struct ext4_group_desc *out_gd);
static int ext4_write_group_desc(struct super_block *sb, uint32_t group,
				 const struct ext4_group_desc *gd);

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
	struct ext4_group_desc gd_local;
	uint32_t bitmap_block;
	int ret;
	
	if (!sbi || !bitmap_buf) {
		return 0;
	}
	if (group >= sbi->s_groups_count) {
		return 0;
	}
	if (ext4_read_group_desc(sb, group, &gd_local) < 0) {
		return 0;
	}
	bitmap_block = gd_local.bg_block_bitmap_lo;
	
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

static int ext4_read_group_desc(struct super_block *sb, uint32_t group,
				struct ext4_group_desc *out_gd)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t gd_base;
	uint32_t desc_off;
	uint32_t blk, off;
	char *buf;
	uint32_t copy_sz;
	int ret;

	if (!sbi || !out_gd || sbi->s_desc_size == 0) return -1;
	gd_base = sbi->s_first_data_block + 1;
	if (block_size == 1024) gd_base = 2;
	desc_off = group * (uint32_t)sbi->s_desc_size;
	blk = gd_base + (desc_off / block_size);
	off = desc_off % block_size;
	copy_sz = sbi->s_desc_size;
	if (copy_sz > sizeof(struct ext4_group_desc)) copy_sz = sizeof(struct ext4_group_desc);
	if (off + copy_sz > block_size) return -1;
	buf = (char *)malloc(block_size);
	if (!buf) return -1;
	ret = ext4_read_block(blk, buf);
	if (ret < 0) { free(buf); return -1; }
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
	uint32_t blk, off;
	char *buf;
	uint32_t copy_sz;
	int ret;

	if (!sbi || !gd || sbi->s_desc_size == 0) return -1;
	gd_base = sbi->s_first_data_block + 1;
	if (block_size == 1024) gd_base = 2;
	desc_off = group * (uint32_t)sbi->s_desc_size;
	blk = gd_base + (desc_off / block_size);
	off = desc_off % block_size;
	copy_sz = sbi->s_desc_size;
	if (copy_sz > sizeof(struct ext4_group_desc)) copy_sz = sizeof(struct ext4_group_desc);
	if (off + copy_sz > block_size) return -1;
	buf = (char *)malloc(block_size);
	if (!buf) return -1;
	ret = ext4_read_block(blk, buf);
	if (ret < 0) { free(buf); return -1; }
	memcpy(buf + off, gd, copy_sz);
	ret = ext4_write_block(blk, buf);
	free(buf);
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
	int ret;
	
	if (!sbi || !sbi->s_group_desc) {
		return -1;
	}
	ret = ext4_write_group_desc(sb, group, sbi->s_group_desc);
	return ret;
}

/* 运行时可调，默认使用 safe 模式。 */
static uint32_t ext4_bg_sync_batch = EXT4_TUNING_SAFE_BG_SYNC_BATCH;

uint32_t ext4_get_bg_sync_batch(void)
{
	return ext4_bg_sync_batch;
}

void ext4_set_bg_sync_batch(uint32_t batch)
{
	if (batch == 0) {
		batch = 1;
	}
	ext4_bg_sync_batch = batch;
}

static int ext4_bmap_cache_flush(struct super_block *sb, struct ext4_sb_info *sbi)
{
	struct ext4_group_desc gd_local;
	uint32_t group;
	uint32_t bitmap_block;

	if (!sb || !sbi) return -1;
	if (!sbi->s_bmap_cache_valid || !sbi->s_bmap_cache_dirty) return 0;

	group = sbi->s_bmap_cache_group;
	if (ext4_read_group_desc(sb, group, &gd_local) < 0) return -1;
	bitmap_block = gd_local.bg_block_bitmap_lo;
	if (bitmap_block == 0) return -1;
	if (ext4_write_block_bitmap(sb, bitmap_block, sbi->s_bmap_cache_buf) < 0) return -1;

	sbi->s_bmap_cache_dirty = 0;
	return 0;
}

static int ext4_bmap_cache_load(struct super_block *sb, struct ext4_sb_info *sbi,
				uint32_t group, struct ext4_group_desc *gd_local)
{
	if (!sb || !sbi || !gd_local) return -1;
	if (!sbi->s_bmap_cache_buf) return -1;

	if (sbi->s_bmap_cache_valid && sbi->s_bmap_cache_group == group) {
		return 0;
	}

	if (ext4_bmap_cache_flush(sb, sbi) < 0) return -1;
	if (ext4_read_group_desc(sb, group, gd_local) < 0) return -1;
	if (gd_local->bg_block_bitmap_lo == 0) return -1;
	if (ext4_read_block(gd_local->bg_block_bitmap_lo, sbi->s_bmap_cache_buf) < 0) return -1;

	sbi->s_bmap_cache_group = group;
	sbi->s_bmap_cache_valid = 1;
	sbi->s_bmap_cache_dirty = 0;
	return 0;
}

int ext4_balloc_flush(struct super_block *sb)
{
	struct ext4_sb_info *sbi;
	if (!sb) return -1;
	sbi = (struct ext4_sb_info *)sb->s_fs_info;
	if (!sbi) return -1;

	if (ext4_bmap_cache_flush(sb, sbi) < 0) return -1;
	if (sbi->s_bg_sync_pending > 0) {
		if (ext4_sync_super_free_counts(sb) < 0) return -1;
		sbi->s_bg_sync_pending = 0;
	}
	return 0;
}

/**
 * ext4_new_block - 分配一个新块
 * @sb: 超级块
 * 
 * 从块位图中找到第一个空闲块，标记为已使用
 * 返回分配的块号，失败返回 0
 */
uint32_t ext4_new_blocks_in_group(struct super_block *sb, uint32_t goal_len,
				  uint32_t *out_len, uint32_t preferred_group)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t block_size = ext4_get_block_size();
	uint32_t blocks_per_group;
	uint32_t blocks_count = ext4_get_blocks_count();
	uint32_t dev_blocks = 0;
	uint32_t new_block = 0, alloc_len = 0;
	struct ext4_group_desc gd_local;
	uint32_t i, start_i, limit_i;
	uint32_t g, start_group;
	uint32_t groups_scanned = 0;
	uint32_t groups_count;
	int ret;
	
	if (!sbi || !sbi->s_group_desc) {
		return 0;
	}
	if (!sbi->s_bmap_cache_buf) {
		return 0;
	}
	if (block_size == 0) {
		return 0;
	}
	/* 一个块位图可表示的块数；按 ext4 位图布局，这是每组可分配上限。 */
	blocks_per_group = block_size * 8;
	if (blocks_per_group == 0) {
		return 0;
	}
	/* blocks_count 必须来自挂载时解析的 on-disk superblock。 */
	/* 以多个来源交叉约束几何，避免任一来源漂移导致越界分配。 */
	if (sbi->s_blocks_count > 0) {
		if (blocks_count == 0 || sbi->s_blocks_count < blocks_count) {
			blocks_count = sbi->s_blocks_count;
		}
	}
	if (blocks_count == 0) return 0;
	{
		uint32_t sectors_per_block = block_size / ATA_SECTOR_SIZE;
		uint32_t total_sectors = ata_get_total_sectors();
		if (sectors_per_block == 0) sectors_per_block = 1;
		if (total_sectors > 0) {
			dev_blocks = total_sectors / sectors_per_block;
			if (dev_blocks > 0 && (blocks_count == 0 || blocks_count > dev_blocks)) {
				blocks_count = dev_blocks;
			}
		}
	}
	if (goal_len == 0) {
		goal_len = 1;
	}
	if (blocks_count == 0 || blocks_per_group == 0) {
		return 0;
	}
	if (blocks_per_group > blocks_count) {
		blocks_per_group = blocks_count;
	}
	if (sbi->s_groups_count > 0) {
		uint32_t by_groups = sbi->s_groups_count * blocks_per_group;
		if (by_groups > 0 && by_groups < blocks_count) {
			blocks_count = by_groups;
		}
	}
	groups_count = (blocks_count + blocks_per_group - 1) / blocks_per_group;
	if (groups_count == 0) {
		return 0;
	}

	/* 先尝试请求级首选组，再回退到全局 goal（next-fit）。 */
	start_group = sbi->s_alloc_nf.goal_group;
	if (preferred_group < groups_count) {
		start_group = preferred_group;
	}
	if (start_group >= groups_count) start_group = 0;

	for (groups_scanned = 0; groups_scanned < groups_count; groups_scanned++) {
		uint32_t group_blocks;
		uint32_t group_start;
		char *bitmap_buf;
		int found = 0;

		g = start_group + groups_scanned;
		if (g >= groups_count) g -= groups_count;

		group_blocks = blocks_per_group;
		group_start = g * blocks_per_group;
		if (group_start >= blocks_count) {
			continue;
		}
		if (group_start + group_blocks > blocks_count)
			group_blocks = blocks_count - group_start;
		if (group_blocks <= 1) continue;
		if (ext4_read_group_desc(sb, g, &gd_local) < 0) continue;
		if (gd_local.bg_block_bitmap_lo == 0) continue;
		ret = ext4_bmap_cache_load(sb, sbi, g, &gd_local);
		if (ret < 0) continue;

		bitmap_buf = sbi->s_bmap_cache_buf;

		/* 组内 next-fit：每组独立游标；无表时从 bit 1 起扫 */
		start_i = 1;
		if (sbi->s_alloc_nf.goal_bit_per_group && g < sbi->s_groups_count) {
			uint32_t gb = sbi->s_alloc_nf.goal_bit_per_group[g];
			if (gb >= 1 && gb < group_blocks)
				start_i = gb;
		}

		limit_i = group_blocks;
		for (i = start_i; i < limit_i; i++) {
			uint32_t byte = i / 8, bit = i % 8;
			if (byte >= block_size) break;
			if (!(bitmap_buf[byte] & (1 << bit))) {
				uint32_t run = 1;
				uint32_t j;
				uint32_t run_max = goal_len;
				if (run_max > (group_blocks - i)) {
					run_max = group_blocks - i;
				}
				for (j = i + 1; j < i + run_max; j++) {
					uint32_t b2 = j / 8, bt2 = j % 8;
					if (b2 >= block_size) break;
					if (bitmap_buf[b2] & (1 << bt2)) break;
					run++;
				}
				for (j = 0; j < run; j++) {
					uint32_t k = i + j;
					uint32_t b3 = k / 8, bt3 = k % 8;
					bitmap_buf[b3] |= (1 << bt3);
				}
				new_block = group_start + i;
				alloc_len = run;
				found = 1;
				break;
			}
		}

		if (!found && start_i > 1) {
			for (i = 1; i < start_i; i++) {
				uint32_t byte = i / 8, bit = i % 8;
				if (byte >= block_size) break;
				if (!(bitmap_buf[byte] & (1 << bit))) {
					uint32_t run = 1;
					uint32_t j;
					uint32_t run_max = goal_len;
					if (run_max > (group_blocks - i)) {
						run_max = group_blocks - i;
					}
					for (j = i + 1; j < i + run_max; j++) {
						uint32_t b2 = j / 8, bt2 = j % 8;
						if (b2 >= block_size) break;
						if (bitmap_buf[b2] & (1 << bt2)) break;
						run++;
					}
					for (j = 0; j < run; j++) {
						uint32_t k = i + j;
						uint32_t b3 = k / 8, bt3 = k % 8;
						bitmap_buf[b3] |= (1 << bt3);
					}
					new_block = group_start + i;
					alloc_len = run;
					found = 1;
					break;
				}
			}
		}
		if (!found) continue;
		if (new_block < group_start || new_block >= blocks_count) {
			return 0;
		}
		if (alloc_len == 0) {
			return 0;
		}
		if (new_block + alloc_len > blocks_count) {
			return 0;
		}

		sbi->s_bmap_cache_dirty = 1;

		ret = ext4_bmap_cache_flush(sb, sbi);
		if (ret < 0) return 0;

		if (gd_local.bg_free_blocks_count_lo > alloc_len) {
			gd_local.bg_free_blocks_count_lo = (uint16_t)(gd_local.bg_free_blocks_count_lo - alloc_len);
		} else {
			gd_local.bg_free_blocks_count_lo = 0;
		}
		if (ext4_write_group_desc(sb, g, &gd_local) < 0) return 0;
		if (g == 0) *sbi->s_group_desc = gd_local;

		/* 更新 next-fit 游标：每组独立；跨组起点仍用 goal_group */
		{
			uint32_t next_bit = i + alloc_len;
			if (sbi->s_alloc_nf.goal_bit_per_group && g < sbi->s_groups_count) {
				if (next_bit >= group_blocks) {
					sbi->s_alloc_nf.goal_bit_per_group[g] = 1;
					sbi->s_alloc_nf.goal_group =
						(g + 1 < groups_count) ? (g + 1) : 0;
				} else {
					sbi->s_alloc_nf.goal_bit_per_group[g] = next_bit;
					sbi->s_alloc_nf.goal_group = g;
				}
			} else {
				if (next_bit >= group_blocks) {
					sbi->s_alloc_nf.goal_group =
						(g + 1 < groups_count) ? (g + 1) : 0;
				} else {
					sbi->s_alloc_nf.goal_group = g;
				}
			}
		}

		/* Linux 类似的延迟统计更新：按运行时参数批量同步 super free count */
		sbi->s_bg_sync_pending++;
		if (sbi->s_bg_sync_pending >= ext4_get_bg_sync_batch()) {
			(void)ext4_sync_super_free_counts(sb);
			sbi->s_bg_sync_pending = 0;
		}
		break;
	}
	if (new_block == 0) return 0;
	if (out_len) {
		*out_len = alloc_len;
	}

	return new_block;
}

uint32_t ext4_new_blocks(struct super_block *sb, uint32_t goal_len, uint32_t *out_len)
{
	return ext4_new_blocks_in_group(sb, goal_len, out_len, (uint32_t)-1);
}

uint32_t ext4_new_block(struct super_block *sb)
{
	uint32_t alloc_len = 0;
	return ext4_new_blocks(sb, 1, &alloc_len);
}

uint32_t ext4_new_block_in_group(struct super_block *sb, uint32_t preferred_group)
{
	uint32_t alloc_len = 0;
	return ext4_new_blocks_in_group(sb, 1, &alloc_len, preferred_group);
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
	struct ext4_group_desc gd_local;
	int ret;
	
	if (!sbi || !sbi->s_group_desc) {
		return -1;
	}
	
	if (group >= sbi->s_groups_count) return -1;
	
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
	
	if (ext4_read_group_desc(sb, group, &gd_local) < 0) {
		free(bitmap_buf);
		return -1;
	}
	gd_local.bg_free_blocks_count_lo++;
	ret = ext4_write_group_desc(sb, group, &gd_local);
	if (ret < 0) {
		free(bitmap_buf);
		return ret;
	}
	if (group == 0) *sbi->s_group_desc = gd_local;

	sbi->s_bg_sync_pending++;
	if (sbi->s_bg_sync_pending >= ext4_get_bg_sync_batch()) {
		(void)ext4_sync_super_free_counts(sb);
		sbi->s_bg_sync_pending = 0;
	}

	/* 释放会改变位图，避免和缓存冲突，直接使缓存失效。 */
	if (sbi->s_bmap_cache_valid && sbi->s_bmap_cache_group == group) {
		sbi->s_bmap_cache_valid = 0;
		sbi->s_bmap_cache_dirty = 0;
	}

	free(bitmap_buf);

	return ret;
}

