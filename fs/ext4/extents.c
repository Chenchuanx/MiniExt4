/*
 * Ext4 简化版 extents 机制
 *
 * 设计说明：
 * - 仅针对普通文件的数据块做 extents 映射；
 * - 每个文件的 extents 列表保存在一个“索引块”中，索引块号存放在 on-disk inode 的 i_block[0] 中；
 * - 目前实现为单级 extents 数组：一个索引块内直接存放若干 extent，不再构建多层 B+tree；
 * - 目录仍然使用 ext4_dir.c 中的直接块数组 i_block[0..11] 存放目录块。
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
extern uint32_t ext4_new_block(struct super_block *sb);
extern uint32_t ext4_new_blocks(struct super_block *sb, uint32_t goal_len, uint32_t *out_len);

/* 简化的内存操作函数（与其他 Ext4 源文件保持一致） */
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

#define EXT4_PREALLOC_GOAL_LEN 32U


/* 在 extents 索引块中查找覆盖给定逻辑块的 extent
 * 成功找到时：
 *   - 返回该 extent 在数组中的索引（>=0）
 *   - 通过 *out_extent 返回指向 extent 的指针
 * 未找到时：
 *   - 返回 -1
 *   - 如果 out_prev_index 非空，则返回“最后一个 < lblock 的 extent 的索引”（用于插入逻辑）
 */
static int ext4_extents_find(struct ext4_extent_header *eh, uint32_t lblock,
			     struct ext4_extent **out_extent, int *out_prev_index)
{
	uint32_t i;
	struct ext4_extent *extents;
	int last_less = -1;

	if (!eh) {
		return -1;
	}

	if (eh->eh_magic != EXT4_EXT_MAGIC) {
		return -1;
	}

	if (eh->eh_entries == 0) {
		if (out_prev_index) {
			*out_prev_index = -1;
		}
		return -1;
	}

	extents = (struct ext4_extent *)(eh + 1);

	for (i = 0; i < eh->eh_entries; i++) {
		uint32_t ee_block = extents[i].ee_block;
		uint32_t ee_len   = extents[i].ee_len;

		if (ee_len == 0) {
			continue;
		}

		if (ee_block <= lblock && lblock < ee_block + ee_len) {
			if (out_extent) {
				*out_extent = &extents[i];
			}
			if (out_prev_index) {
				*out_prev_index = (int)i - 1;
			}
			return (int)i;
		}

		if (ee_block < lblock) {
			last_less = (int)i;
		} else if (ee_block > lblock) {
			break;
		}
	}

	if (out_prev_index) {
		*out_prev_index = last_less;
	}
	return -1;
}

/* 在 extents 索引块中插入一个新 extent，或合并到相邻的 extent 中
 * 要求：新 extent 的 ee_len 必须为 1（单块），由上层分配。
 *
 * 返回 0 表示成功，负数表示失败。
 */
static int ext4_extents_insert(struct ext4_extent_header *eh,
			       struct ext4_extent *new_ex)
{
	struct ext4_extent *extents;
	uint32_t entries;
	uint32_t max;
	int i;

	if (!eh || !new_ex) {
		return -1;
	}

	if (eh->eh_magic != EXT4_EXT_MAGIC) {
		return -1;
	}

	extents = (struct ext4_extent *)(eh + 1);
	entries = eh->eh_entries;
	max = eh->eh_max;

	if (entries > max) {
		return -1;
	}

	/* 首先尝试与相邻 extent 合并，减少碎片和条目数量 */
	for (i = 0; i < (int)entries; i++) {
		uint32_t ee_block = extents[i].ee_block;
		uint32_t ee_len   = extents[i].ee_len;
		uint64_t ee_start = ((uint64_t)extents[i].ee_start_hi << 32) |
				    (uint64_t)extents[i].ee_start_lo;
		uint64_t new_start = ((uint64_t)new_ex->ee_start_hi << 32) |
				     (uint64_t)new_ex->ee_start_lo;

		/* 尾部相邻： [ee_block, ee_block+ee_len-1] 后面紧跟 new_ex */
		if (ee_block + ee_len == new_ex->ee_block &&
		    ee_start + ee_len == new_start) {
			extents[i].ee_len = (uint16_t)(ee_len + new_ex->ee_len);
			return 0;
		}

		/* 头部相邻：new_ex 在前，紧挨着当前 extent */
		if (new_ex->ee_block + new_ex->ee_len == ee_block &&
		    new_start + new_ex->ee_len == ee_start) {
			extents[i].ee_block    = new_ex->ee_block;
			extents[i].ee_start_lo = new_ex->ee_start_lo;
			extents[i].ee_start_hi = new_ex->ee_start_hi;
			extents[i].ee_len      = (uint16_t)(ee_len + new_ex->ee_len);
			return 0;
		}
	}

	/* 不能合并时，按逻辑块号有序插入 */
	if (entries == max) {
		/* 节点已满：当前简化实现直接报错（不做 B+Tree 分裂） */
		printf("MiniExt4: extent node full, cannot insert new extent\n");
		return -1;
	}

	/* 找到插入位置：保持按 ee_block 升序 */
	i = (int)entries;
	while (i > 0 && extents[i - 1].ee_block > new_ex->ee_block) {
		extents[i] = extents[i - 1];
		i--;
	}
	extents[i] = *new_ex;
	eh->eh_entries = (uint16_t)(entries + 1);

	return 0;
}

/* 初始化一个新的 root extents 头部（挂在 inode->i_block 中） */
static void ext4_extents_init_root(struct ext4_extent_header *eh, uint32_t bytes)
{
	uint32_t max;

	if (!eh) {
		return;
	}

	memset(eh, 0, bytes);
	eh->eh_magic = (uint16_t)EXT4_EXT_MAGIC;
	eh->eh_entries = 0;
	eh->eh_depth = 0; /* 单层叶子 */
	eh->eh_generation = 0;

	max = (bytes - (uint32_t)sizeof(struct ext4_extent_header)) /
	      (uint32_t)sizeof(struct ext4_extent);
	eh->eh_max = (uint16_t)max;
}

/* 公开接口：基于 extents 的数据块映射
 *
 * @inode:  文件 inode
 * @lblock: 逻辑块号（从 0 开始）
 * @create: 为 1 时允许分配新块；为 0 时只查询已存在映射
 * @out_block: 返回的数据块号（0 表示空洞或失败）
 * @is_new:  返回该数据块是否是新分配的（仅在 create=1 时有意义）
 *
 * 成功返回 0，失败返回 -1。
 */
int ext4_extents_get_block(struct inode *inode, uint32_t lblock,
			   int create, uint32_t *out_block, int *is_new)
{
	struct super_block *sb;
	struct ext4_inode_info *ei;
	uint32_t block_size;
	char *buf;
	uint32_t index_block;
	int ret;

	if (!inode || !out_block) {
		return -1;
	}

	sb = inode->i_sb;
	ei = (struct ext4_inode_info *)inode->i_private;
	block_size = ext4_get_block_size();

	if (!sb || !ei || block_size == 0) {
		return -1;
	}

	*out_block = 0;
	if (is_new) {
		*is_new = 0;
	}

	if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode)) {
		return -1;
	}

	/* 根 extents 头部直接挂在 i_block 数组里（与 Linux ext4 一致） */
	{
		struct ext4_extent_header *eh_root;
		struct ext4_extent *ex;
		int prev_index;
		int idx;

		eh_root = (struct ext4_extent_header *)ei->i_block;

		/* 如未初始化：仅在 create==1 时初始化根节点 */
		if (eh_root->eh_magic != EXT4_EXT_MAGIC) {
			if (!create) {
				return 0;
			}
			ext4_extents_init_root(eh_root, sizeof(ei->i_block));
		}

		/* 仅支持单层叶子（eh_depth==0） */
		if (eh_root->eh_depth != 0) {
			return -1;
		}

		/* 先尝试在现有 extents 中查找映射 */
		idx = ext4_extents_find(eh_root, lblock, &ex, &prev_index);
		if (idx >= 0 && ex) {
			uint32_t ee_block = ex->ee_block;
			uint64_t phys_start = ((uint64_t)ex->ee_start_hi << 32) |
					      (uint64_t)ex->ee_start_lo;
			uint32_t phys = (uint32_t)(phys_start + (lblock - ee_block));

			*out_block = phys;
			if (is_new) {
				*is_new = 0;
			}
			return 0;
		}

		if (!create) {
			/* 仅查询，不分配新块 */
			return 0;
		}

		/* 分配一个新的物理块，并插入/合并到 extents 列表中
		 * 当前实现每次只为单个逻辑块分配一个物理块，由 ext4_extents_insert
		 * 在连续写入时做 extent 合并。
		 */
		{
			uint32_t alloc_len = 1;
			uint32_t new_block = ext4_new_blocks(sb, EXT4_PREALLOC_GOAL_LEN, &alloc_len);
			struct ext4_extent new_ex;

			if (new_block == 0) {
				return -1;
			}

			memset(&new_ex, 0, sizeof(new_ex));
			new_ex.ee_block    = lblock;
			if (alloc_len == 0) {
				alloc_len = 1;
			}
			if (alloc_len > 0x7fffU) {
				alloc_len = 0x7fffU;
			}
			new_ex.ee_len      = (uint16_t)alloc_len;
			new_ex.ee_start_lo = new_block;
			new_ex.ee_start_hi = 0;

			ret = ext4_extents_insert(eh_root, &new_ex);
			if (ret < 0) {
				return -1;
			}

			*out_block = new_block;
			if (is_new) {
				*is_new = 1;
			}
		}
	}

	return 0;
}

