/*
 * Ext4 简化版 extents 机制
 *
 * 设计说明：
 * - 普通文件的数据块映射采用 Linux ext4 同款 extent B+ 树；
 * - 小树时 root 内嵌在 inode 的 i_block[60 字节] 区域；树变大后 root 仍为内嵌索引，
 *   子节点分配为独立磁盘块（path[].blocknr != 0）；
 * - 目录默认仍走 ext4_dir.c 的直接块 / 间接块，也可选 extents（见 dir.c）。
 *
 * 树结构概要（与 Linux fs/ext4/extents.c 一致）：
 *   [root: eh_depth=N] --索引--> [内部节点: eh_depth=N-1] ... --> [叶子: eh_depth=0, extent 数组]
 *   索引项 ei_block = 右子树最小逻辑块号；ei_leaf_* = 子节点物理块号。
 *   叶子项 ee_block/ee_len = 逻辑区间；ee_start_* = 物理起始块。
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
extern uint32_t ext4_get_blocks_count(void);
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

/* 运行时可调，默认使用 safe 模式。 */
static uint32_t ext4_prealloc_goal_len = EXT4_TUNING_SAFE_PREALLOC_GOAL_LEN;
#define EXT4_EXT_MAX_DEPTH 5	/* 与 Linux ext4 一致的最大树高 */
/* 单次 extent 最大长度（ee_len 为 16 bit，高 bit 另有 unwritten 语义，此处未实现） */
#define EXT4_EXT_INIT_MAX_LEN 0x7fffU

uint32_t ext4_get_prealloc_goal_len(void)
{
	return ext4_prealloc_goal_len;
}

void ext4_set_prealloc_goal_len(uint32_t goal_len)
{
	if (goal_len == 0) {
		goal_len = 1;
	}
	ext4_prealloc_goal_len = goal_len;
}

/* extent 树遍历路径：path[0] 为 inode 内嵌 root，path[depth] 为叶子 */
struct ext4_ext_path {
	struct ext4_extent_header *eh;	/* 当前层节点头（可能在 buf 或 i_block 内） */
	struct ext4_extent_idx *idx;	/* 本层选中的索引项（root 层为 NULL） */
	uint32_t blocknr;		/* 节点物理块号；0 表示内嵌在 inode，无需写盘 */
	char *buf;			/* 外部节点读缓冲；内嵌 root/叶在 inode 上时为 NULL */
};

/* 48-bit 物理块号编解码（与 on-disk ext4_extent / ext4_extent_idx 一致） */
static uint64_t ext4_extent_pblock(const struct ext4_extent *ex)
{
	return ((uint64_t)ex->ee_start_hi << 32) | (uint64_t)ex->ee_start_lo;
}

static void ext4_extent_set_pblock(struct ext4_extent *ex, uint64_t pblk)
{
	ex->ee_start_hi = (uint16_t)(pblk >> 32);
	ex->ee_start_lo = (uint32_t)(pblk & 0xffffffffU);
}

static uint64_t ext4_idx_pblock(const struct ext4_extent_idx *ix)
{
	return ((uint64_t)ix->ei_leaf_hi << 32) | (uint64_t)ix->ei_leaf_lo;
}

static void ext4_idx_set_pblock(struct ext4_extent_idx *ix, uint64_t pblk)
{
	ix->ei_leaf_hi = (uint16_t)(pblk >> 32);
	ix->ei_leaf_lo = (uint32_t)(pblk & 0xffffffffU);
}

static void ext4_extents_init_node(struct ext4_extent_header *eh, uint32_t bytes, uint16_t depth)
{
	uint32_t max;

	if (!eh || bytes < sizeof(struct ext4_extent_header)) {
		return;
	}
	memset(eh, 0, bytes);
	eh->eh_magic = (uint16_t)EXT4_EXT_MAGIC;
	eh->eh_entries = 0;
	eh->eh_depth = depth;
	eh->eh_generation = 0;
	/* depth==0 为叶子（存 extent）；depth>0 为索引（存 extent_idx） */
	if (depth == 0) {
		max = (bytes - (uint32_t)sizeof(struct ext4_extent_header)) /
		      (uint32_t)sizeof(struct ext4_extent);
	} else {
		max = (bytes - (uint32_t)sizeof(struct ext4_extent_header)) /
		      (uint32_t)sizeof(struct ext4_extent_idx);
	}
	eh->eh_max = (uint16_t)max;
}


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
 *
 * 合并条件：逻辑块连续且物理块也连续（left/right neighbor）。
 * 不能合并且 entries==max 时返回 -1，由上层触发 ext4_extents_split_leaf。
 *
 * 返回 0 表示成功，-1 表示失败。
 */
static int ext4_extents_insert(struct ext4_extent_header *eh,
			       struct ext4_extent *new_ex)
{
	struct ext4_extent *extents;
	uint32_t entries;
	uint32_t max;
	int insert_pos;
	int i;
	uint64_t new_start;

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

	/* 找到按逻辑块号排序的插入位置。 */
	insert_pos = (int)entries;
	while (insert_pos > 0 && extents[insert_pos - 1].ee_block > new_ex->ee_block) {
		insert_pos--;
	}

	/* 只检查插入点两侧邻居即可判定是否可合并，避免全表线性扫描。 */
	new_start = ext4_extent_pblock(new_ex);
	if (insert_pos > 0) {
		struct ext4_extent *left = &extents[insert_pos - 1];
		uint64_t left_start = ext4_extent_pblock(left);
		uint32_t merged_len;

		if (left->ee_block + left->ee_len == new_ex->ee_block &&
		    left_start + left->ee_len == new_start) {
			merged_len = (uint32_t)left->ee_len + (uint32_t)new_ex->ee_len;
			if (merged_len <= EXT4_EXT_INIT_MAX_LEN) {
				left->ee_len = (uint16_t)merged_len;

				/* 合并到左邻后，再尝试与右邻融合，进一步减少条目数。 */
				if (insert_pos < (int)entries) {
					struct ext4_extent *right = &extents[insert_pos];
					uint64_t merged_start = ext4_extent_pblock(left);
					if (left->ee_block + left->ee_len == right->ee_block &&
					    merged_start + left->ee_len == ext4_extent_pblock(right)) {
						uint32_t total_len = (uint32_t)left->ee_len + (uint32_t)right->ee_len;
						if (total_len <= EXT4_EXT_INIT_MAX_LEN) {
							left->ee_len = (uint16_t)total_len;
							for (i = insert_pos; i < (int)entries - 1; i++) {
								extents[i] = extents[i + 1];
							}
							eh->eh_entries = (uint16_t)(entries - 1);
						}
					}
				}
				return 0;
			}
		}
	}

	if (insert_pos < (int)entries) {
		struct ext4_extent *right = &extents[insert_pos];
		uint32_t merged_len;

		if (new_ex->ee_block + new_ex->ee_len == right->ee_block &&
		    new_start + new_ex->ee_len == ext4_extent_pblock(right)) {
			merged_len = (uint32_t)right->ee_len + (uint32_t)new_ex->ee_len;
			if (merged_len <= EXT4_EXT_INIT_MAX_LEN) {
				right->ee_block = new_ex->ee_block;
				right->ee_start_lo = new_ex->ee_start_lo;
				right->ee_start_hi = new_ex->ee_start_hi;
				right->ee_len = (uint16_t)merged_len;
				return 0;
			}
		}
	}

	/* 不能合并时，按逻辑块号有序插入。 */
	if (entries == max) {
		return -1;
	}

	for (i = (int)entries; i > insert_pos; i--) {
		extents[i] = extents[i - 1];
	}
	extents[insert_pos] = *new_ex;
	eh->eh_entries = (uint16_t)(entries + 1);

	return 0;
}

/* 初始化一个新的 root extents 头部（挂在 inode->i_block 中） */
static void ext4_extents_init_root(struct ext4_extent_header *eh, uint32_t bytes)
{
	ext4_extents_init_node(eh, bytes, 0);
}

static void ext4_extents_free_path(struct ext4_ext_path *path, int depth)
{
	int i;
	if (!path) {
		return;
	}
	/* 仅释放 read_path 中为外部节点 malloc 的 buf；内嵌 root 无 buf */
	for (i = 0; i <= depth; i++) {
		if (path[i].buf) {
			free(path[i].buf);
			path[i].buf = (char *)0;
		}
	}
}

/*
 * 自 root 沿索引下降到覆盖 lblock 的叶子，填充 path[]。
 * 每层选「ei_block <= lblock 的最大索引项」（与 Linux ext4_find_extent 相同规则）。
 */
static int ext4_extents_read_path(struct inode *inode, uint32_t lblock,
				  struct ext4_ext_path *path, int *out_depth)
{
	struct ext4_inode_info *ei;
	struct ext4_extent_header *eh;
	uint32_t block_size;
	int d;

	if (!inode || !inode->i_private || !path || !out_depth) {
		return -1;
	}

	ei = (struct ext4_inode_info *)inode->i_private;
	eh = (struct ext4_extent_header *)ei->i_block;
	block_size = ext4_get_block_size();

	if (eh->eh_magic != EXT4_EXT_MAGIC) {
		return -1;
	}
	if (eh->eh_depth > EXT4_EXT_MAX_DEPTH) {
		return -1;
	}

	memset(path, 0, sizeof(struct ext4_ext_path) * (EXT4_EXT_MAX_DEPTH + 1));
	path[0].eh = eh;
	path[0].blocknr = 0;
	path[0].idx = (struct ext4_extent_idx *)0;

	for (d = 0; d < (int)eh->eh_depth; d++) {
		struct ext4_extent_header *cur = path[d].eh;
		struct ext4_extent_idx *idxs;
		struct ext4_extent_idx *chosen;
		uint32_t i;
		uint32_t child_blk;
		char *buf;

		if (cur->eh_entries == 0) {
			return -1;
		}
		idxs = (struct ext4_extent_idx *)(cur + 1);
		chosen = &idxs[0];
		for (i = 0; i < cur->eh_entries; i++) {
			if (idxs[i].ei_block <= lblock) {
				chosen = &idxs[i];
			} else {
				break;
			}
		}

		child_blk = (uint32_t)ext4_idx_pblock(chosen);
		if (child_blk == 0) {
			return -1;
		}
		buf = (char *)malloc(block_size);
		if (!buf) {
			return -1;
		}
		if (ext4_read_block(child_blk, buf) < 0) {
			free(buf);
			return -1;
		}

		path[d + 1].buf = buf;
		path[d + 1].eh = (struct ext4_extent_header *)buf;
		path[d + 1].blocknr = child_blk;
		path[d + 1].idx = chosen;
		if (path[d + 1].eh->eh_magic != EXT4_EXT_MAGIC) {
			return -1;
		}
		if (path[d + 1].eh->eh_depth != (uint16_t)(eh->eh_depth - (d + 1))) {
			return -1;
		}
	}

	*out_depth = (int)eh->eh_depth;
	return 0;
}

/* 在索引/叶子条目数组中插入一个元素（memmove 版，供 ext4_extent_idx 分裂使用） */
static int ext4_extents_insert_entry(void *base, uint16_t *entries, uint16_t max,
				     uint32_t elem_size, int pos, const void *new_elem)
{
	char *arr = (char *)base;
	int n = (int)(*entries);
	int i;

	if (!arr || !entries || !new_elem || elem_size == 0) {
		return -1;
	}
	if (n < 0 || n > (int)max || n == (int)max) {
		return -1;
	}
	if (pos < 0) pos = 0;
	if (pos > n) pos = n;

	for (i = n; i > pos; i--) {
		memcpy(arr + (uint32_t)i * elem_size,
		       arr + (uint32_t)(i - 1) * elem_size,
		       elem_size);
	}
	memcpy(arr + (uint32_t)pos * elem_size, new_elem, elem_size);
	*entries = (uint16_t)(n + 1);
	return 0;
}

/* 取有效文件系统块总数（sbi 与全局计数取较小值，防止越界） */
static uint32_t ext4_extents_fs_blocks_count(struct super_block *sb)
{
	struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
	uint32_t fs_blocks_count = ext4_get_blocks_count();

	if (sbi && sbi->s_blocks_count > 0 &&
	    (fs_blocks_count == 0 || sbi->s_blocks_count < fs_blocks_count)) {
		fs_blocks_count = sbi->s_blocks_count;
	}
	return fs_blocks_count;
}

/*
 * 写回 path 上某层外部节点。
 * blocknr==0 表示节点内嵌在 inode（root），修改已在内存 i_block 中，无需 ext4_write_block。
 */
static int ext4_extents_write_path_node(struct ext4_ext_path *path, int level)
{
	if (path[level].blocknr != 0 &&
	    ext4_write_block(path[level].blocknr, path[level].buf) < 0) {
		return -1;
	}
	return 0;
}

/* 在叶子中查找 lblock 的物理块；找到返回 1，未映射返回 0，失败返回 -1 */
static int ext4_extents_lookup_mapped(struct ext4_extent_header *leaf, uint32_t lblock,
				      uint32_t *out_block)
{
	struct ext4_extent *ex;
	int prev_index;
	int idx;

	idx = ext4_extents_find(leaf, lblock, &ex, &prev_index);
	(void)prev_index;
	if (idx >= 0 && ex) {
		uint32_t ee_block = ex->ee_block;
		uint64_t phys_start = ext4_extent_pblock(ex);

		*out_block = (uint32_t)(phys_start + (lblock - ee_block));
		return 1;
	}
	return 0;
}

/* 分配物理块并构造待插入的 extent（含预分配记账） */
static int ext4_extents_alloc_new_extent(struct inode *inode, struct super_block *sb,
					 uint32_t lblock, uint32_t fs_blocks_count,
					 struct ext4_extent *new_ex, uint32_t *out_pblock)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;
	uint32_t block_size = ext4_get_block_size();
	uint32_t preferred_group = ei ? ei->i_alloc_group_hint : (uint32_t)-1;
	uint32_t alloc_len = 1;
	uint32_t goal_len = ext4_get_prealloc_goal_len();
	uint32_t new_block;

	new_block = ext4_new_blocks_in_group(sb, goal_len, &alloc_len, preferred_group);
	if (new_block == 0 || fs_blocks_count == 0 || new_block >= fs_blocks_count) {
		return -1;
	}

	memset(new_ex, 0, sizeof(*new_ex));
	new_ex->ee_block = lblock;
	if (alloc_len == 0) {
		alloc_len = 1;
	}
	if (alloc_len > EXT4_EXT_INIT_MAX_LEN) {
		alloc_len = EXT4_EXT_INIT_MAX_LEN;
	}
	if (new_block + alloc_len > fs_blocks_count) {
		alloc_len = fs_blocks_count - new_block;
		if (alloc_len == 0) {
			return -1;
		}
	}
	new_ex->ee_len = (uint16_t)alloc_len;
	ext4_extent_set_pblock(new_ex, (uint64_t)new_block);

	/* file.c 会对 is_new 记 1 个块，这里补上其余预分配块的 i_blocks 记账。 */
	if (alloc_len > 1) {
		uint32_t sectors_per_block = (block_size + 511U) / 512U;

		inode->i_blocks += (unsigned long)(alloc_len - 1U) *
				   (unsigned long)sectors_per_block;
		inode->i_state |= I_DIRTY;
	}

	*out_pblock = new_block;
	return 0;
}

/* 叶子有空间时直接插入 extent（对应 Linux ext4_ext_insert_extent 的 fast path） */
static int ext4_extents_insert_at_leaf(struct ext4_ext_path *path, int depth,
				       struct ext4_extent *new_ex, uint32_t new_block,
				       uint32_t *out_block, int *is_new)
{
	if (ext4_extents_insert(path[depth].eh, new_ex) < 0) {
		return -1;
	}
	if (ext4_extents_write_path_node(path, depth) < 0) {
		return -1;
	}
	*out_block = new_block;
	if (is_new) {
		*is_new = 1;
	}
	return 0;
}

/*
 * 叶子已满时在插入点分裂（对应 Linux ext4_ext_split）。
 * 成功时 *out_right_blk 为右子物理块号；
 * *out_split_right->ee_block 为右子树最小逻辑块（向上插入索引的 ei_block 键，
 *  不一定是新分配 extent 的起始块——见 insert_pos==n 与 else 分支）。
 */
static int ext4_extents_split_leaf(struct super_block *sb, struct ext4_ext_path *path,
				   int depth, uint32_t block_size, uint32_t fs_blocks_count,
				   const struct ext4_extent *new_ex, uint32_t *out_right_blk,
				   struct ext4_extent *out_split_right)
{
	struct ext4_extent_header *cur_leaf = path[depth].eh;
	struct ext4_extent *leaf_exts = (struct ext4_extent *)(cur_leaf + 1);
	int n = (int)cur_leaf->eh_entries;
	int i;
	int insert_pos;
	int right_count;
	char *right_buf;
	struct ext4_extent_header *right_eh;
	struct ext4_extent *right_exts;
	uint32_t right_blk;

	if (n <= 0 || n != (int)cur_leaf->eh_max) {
		return -1;
	}

	/* 与 Linux ext4 一致：在插入点分裂，而非对半分。 */
	insert_pos = n;
	while (insert_pos > 0 && leaf_exts[insert_pos - 1].ee_block > new_ex->ee_block) {
		insert_pos--;
	}
	right_count = n - insert_pos;

	right_buf = (char *)malloc(block_size);
	if (!right_buf) {
		return -1;
	}
	ext4_extents_init_node((struct ext4_extent_header *)right_buf, block_size, 0);
	right_eh = (struct ext4_extent_header *)right_buf;
	right_exts = (struct ext4_extent *)(right_eh + 1);

	if (right_count > 0) {
		for (i = 0; i < right_count; i++) {
			right_exts[i] = leaf_exts[insert_pos + i];
		}
		right_eh->eh_entries = (uint16_t)right_count;
		cur_leaf->eh_entries = (uint16_t)insert_pos;
		for (i = insert_pos; i < (int)cur_leaf->eh_max; i++) {
			memset(&leaf_exts[i], 0, sizeof(struct ext4_extent));
		}
	}

	if (insert_pos == n) {
		/* 新 extent 整体进入右叶：分界键 = 新 extent 起始逻辑块 */
		right_exts[0] = *new_ex;
		right_eh->eh_entries = 1;
		*out_split_right = *new_ex;
	} else {
		/* 新 extent 留在左叶：分界键 = 右叶第一个 extent 的起始逻辑块 */
		leaf_exts[insert_pos] = *new_ex;
		cur_leaf->eh_entries = (uint16_t)(insert_pos + 1);
		*out_split_right = right_exts[0];
	}

	right_blk = ext4_new_block(sb);
	if (right_blk == 0 || right_blk >= fs_blocks_count) {
		free(right_buf);
		return -1;
	}
	if (ext4_extents_write_path_node(path, depth) < 0) {
		free(right_buf);
		return -1;
	}
	if (ext4_write_block(right_blk, right_buf) < 0) {
		free(right_buf);
		return -1;
	}
	free(right_buf);

	*out_right_blk = right_blk;
	return 0;
}

/*
 * 将右子树（物理块 *right_blk，分界逻辑块 split_right_ex->ee_block）
 * 插入父索引链；索引节点满则继续分裂并向上推进（*right_blk 更新为新索引块号）。
 * 若推到 root 仍满，置 *out_need_grow=1，由 grow_indepth 整树加深。
 */
static int ext4_extents_add_index(struct super_block *sb, struct ext4_ext_path *path,
				  int depth, uint32_t block_size, uint32_t fs_blocks_count,
				  uint32_t *right_blk, struct ext4_extent *split_right_ex,
				  int *out_need_grow)
{
	int level;
	int insert_pos;

	*out_need_grow = 0;
	for (level = depth - 1; level >= 0; ) {
		struct ext4_extent_header *ieh = path[level].eh;
		struct ext4_extent_idx new_idx;
		struct ext4_extent_idx *idxs;
		int j;

		memset(&new_idx, 0, sizeof(new_idx));
		new_idx.ei_block = split_right_ex->ee_block;
		ext4_idx_set_pblock(&new_idx, (uint64_t)*right_blk);

		if (ieh->eh_entries < ieh->eh_max) {
			idxs = (struct ext4_extent_idx *)(ieh + 1);
			insert_pos = ieh->eh_entries;
			for (j = 0; j < (int)ieh->eh_entries; j++) {
				if (idxs[j].ei_block > new_idx.ei_block) {
					insert_pos = j;
					break;
				}
			}
			{
				uint16_t ieh_entries = ieh->eh_entries;

				if (ext4_extents_insert_entry(idxs, &ieh_entries, ieh->eh_max,
							      sizeof(struct ext4_extent_idx),
							      insert_pos, &new_idx) < 0) {
					return -1;
				}
				ieh->eh_entries = ieh_entries;
			}
			if (ext4_extents_write_path_node(path, level) < 0) {
				return -1;
			}
			return 0;
		}

		{
			int nidx = (int)ieh->eh_entries;
			int right_count;
			char *right_ibuf;
			struct ext4_extent_header *right_ieh;
			struct ext4_extent_idx *left_arr;
			struct ext4_extent_idx *right_arr;
			uint32_t new_iblk;

			if (nidx <= 0 || nidx != (int)ieh->eh_max) {
				return -1;
			}
			left_arr = (struct ext4_extent_idx *)(ieh + 1);
			insert_pos = nidx;
			while (insert_pos > 0 && left_arr[insert_pos - 1].ei_block > new_idx.ei_block) {
				insert_pos--;
			}
			right_count = nidx - insert_pos;

			right_ibuf = (char *)malloc(block_size);
			if (!right_ibuf) {
				return -1;
			}
			ext4_extents_init_node((struct ext4_extent_header *)right_ibuf, block_size,
					       (uint16_t)(ieh->eh_depth));
			right_ieh = (struct ext4_extent_header *)right_ibuf;
			right_arr = (struct ext4_extent_idx *)(right_ieh + 1);

			if (right_count > 0) {
				for (j = 0; j < right_count; j++) {
					right_arr[j] = left_arr[insert_pos + j];
				}
				right_ieh->eh_entries = (uint16_t)right_count;
				ieh->eh_entries = (uint16_t)insert_pos;
				for (j = insert_pos; j < (int)ieh->eh_max; j++) {
					memset(&left_arr[j], 0, sizeof(struct ext4_extent_idx));
				}
			}

			if (insert_pos == nidx) {
				right_arr[0] = new_idx;
				right_ieh->eh_entries = 1;
				split_right_ex->ee_block = new_idx.ei_block;
			} else {
				left_arr[insert_pos] = new_idx;
				ieh->eh_entries = (uint16_t)(insert_pos + 1);
				split_right_ex->ee_block = right_arr[0].ei_block;
			}

			new_iblk = ext4_new_block(sb);
			if (new_iblk == 0 || new_iblk >= fs_blocks_count) {
				free(right_ibuf);
				return -1;
			}
			if (ext4_extents_write_path_node(path, level) < 0) {
				free(right_ibuf);
				return -1;
			}
			if (ext4_write_block(new_iblk, right_ibuf) < 0) {
				free(right_ibuf);
				return -1;
			}

			*right_blk = new_iblk;
			free(right_ibuf);
			if (level == 0) {
				*out_need_grow = 1;
				return 0;
			}
			level--;
		}
	}

	*out_need_grow = 1;
	return 0;
}

/*
 * 根索引也满时整树加深（Linux ext4_ext_grow_indepth）：
 * 旧 root 内容搬到新磁盘块，inode 内嵌区改写为 depth+1 的索引，含两个子指针。
 */
static int ext4_extents_grow_indepth(struct super_block *sb, struct ext4_inode_info *ei,
				     uint32_t block_size, uint32_t fs_blocks_count,
				     uint32_t right_blk, struct ext4_extent *split_right_ex)
{
	struct ext4_extent_header *root = (struct ext4_extent_header *)ei->i_block;
	struct ext4_extent_header old_root;
	char *old_root_data;
	uint32_t old_root_blk;
	struct ext4_extent_header *new_right_root;
	struct ext4_extent_idx *root_idx;
	uint32_t old_root_first_lblk;

	if (root->eh_depth >= EXT4_EXT_MAX_DEPTH) {
		return -1;
	}

	old_root = *root;
	old_root_data = (char *)(root + 1);
	if (old_root.eh_entries == 0) {
		return -1;
	}
	old_root_first_lblk = (old_root.eh_depth == 0)
		? ((struct ext4_extent *)old_root_data)[0].ee_block
		: ((struct ext4_extent_idx *)old_root_data)[0].ei_block;

	old_root_blk = ext4_new_block(sb);
	if (old_root_blk == 0 || old_root_blk >= fs_blocks_count) {
		return -1;
	}
	{
		char *tmpbuf = (char *)malloc(block_size);

		if (!tmpbuf) {
			return -1;
		}
		ext4_extents_init_node((struct ext4_extent_header *)tmpbuf, block_size,
				       old_root.eh_depth);
		((struct ext4_extent_header *)tmpbuf)->eh_entries = old_root.eh_entries;
		memcpy(tmpbuf + sizeof(struct ext4_extent_header),
		       old_root_data,
		       (size_t)old_root.eh_entries *
		       (old_root.eh_depth == 0 ? sizeof(struct ext4_extent)
					       : sizeof(struct ext4_extent_idx)));
		if (ext4_write_block(old_root_blk, tmpbuf) < 0) {
			free(tmpbuf);
			return -1;
		}
		free(tmpbuf);
	}

	new_right_root = (struct ext4_extent_header *)ei->i_block;
	ext4_extents_init_node(new_right_root, sizeof(ei->i_block),
			       (uint16_t)(old_root.eh_depth + 1));
	new_right_root->eh_entries = 2;
	root_idx = (struct ext4_extent_idx *)(new_right_root + 1);
	memset(root_idx, 0, sizeof(struct ext4_extent_idx) * 2);
	root_idx[0].ei_block = old_root_first_lblk;
	ext4_idx_set_pblock(&root_idx[0], (uint64_t)old_root_blk);
	root_idx[1].ei_block = split_right_ex->ee_block;
	ext4_idx_set_pblock(&root_idx[1], (uint64_t)right_blk);
	return 0;
}

/*
 * create 路径总控：分配 extent -> 尝试叶插入 -> 叶满则分裂 ->
 * 向上插索引 / 加深 root。返回 lblock 对应的首个物理块（可能来自预分配 run 的起始块）。
 */
static int ext4_extents_insert_mapped_block(struct inode *inode, struct ext4_ext_path *path,
					    int depth, uint32_t lblock, uint32_t fs_blocks_count,
					    uint32_t *out_block, int *is_new)
{
	struct super_block *sb = inode->i_sb;
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;
	uint32_t block_size = ext4_get_block_size();
	struct ext4_extent new_ex;
	struct ext4_extent split_right_ex;
	uint32_t new_block;
	uint32_t right_blk;
	int need_grow;
	struct ext4_extent_header *cur_leaf;

	if (ext4_extents_alloc_new_extent(inode, sb, lblock, fs_blocks_count,
					  &new_ex, &new_block) < 0) {
		return -1;
	}

	if (ext4_extents_insert_at_leaf(path, depth, &new_ex, new_block,
					out_block, is_new) == 0) {
		return 0;
	}

	cur_leaf = path[depth].eh;
	if (cur_leaf->eh_entries < cur_leaf->eh_max) {
		return -1;
	}

	if (ext4_extents_split_leaf(sb, path, depth, block_size, fs_blocks_count,
				    &new_ex, &right_blk, &split_right_ex) < 0) {
		return -1;
	}

	if (ext4_extents_add_index(sb, path, depth, block_size, fs_blocks_count,
				   &right_blk, &split_right_ex, &need_grow) < 0) {
		return -1;
	}

	if (need_grow &&
	    ext4_extents_grow_indepth(sb, ei, block_size, fs_blocks_count,
				      right_blk, &split_right_ex) < 0) {
		return -1;
	}

	*out_block = new_block;
	if (is_new) {
		*is_new = 1;
	}
	return 0;
}

/* 公开接口：基于 extents 的数据块映射
 *
 * @inode:  文件 inode
 * @lblock: 逻辑块号（从 0 开始）
 * @create: 为 1 时允许分配新块；为 0 时只查询已存在映射
 * @out_block: 返回的数据块号（0 表示空洞或未映射）
 * @is_new:  返回该数据块是否是新分配的（仅在 create=1 时有意义）
 *
 * 流程：init root（若需要）-> read_path -> lookup；未命中且 create 则 insert_mapped_block。
 * 成功返回 0，失败返回 -1。
 */
int ext4_extents_get_block(struct inode *inode, uint32_t lblock,
			   int create, uint32_t *out_block, int *is_new)
{
	struct super_block *sb;
	struct ext4_inode_info *ei;
	uint32_t block_size;
	struct ext4_extent_header *eh_root;
	struct ext4_ext_path path[EXT4_EXT_MAX_DEPTH + 1];
	int depth;
	struct ext4_extent_header *leaf;
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

	/* root 内嵌于 ei->i_block；首次 write 时 lazily 初始化空 extent 树 */
	eh_root = (struct ext4_extent_header *)ei->i_block;
	if (eh_root->eh_magic != EXT4_EXT_MAGIC) {
		if (!create) {
			return 0;
		}
		ext4_extents_init_root(eh_root, sizeof(ei->i_block));
	}

	ret = ext4_extents_read_path(inode, lblock, path, &depth);
	if (ret < 0) {
		return -1;
	}

	leaf = path[depth].eh;
	ret = ext4_extents_lookup_mapped(leaf, lblock, out_block);
	if (ret < 0) {
		ext4_extents_free_path(path, depth);
		return -1;
	}
	if (ret > 0) {
		if (is_new) {
			*is_new = 0;
		}
		ext4_extents_free_path(path, depth);
		return 0;
	}

	if (!create) {
		ext4_extents_free_path(path, depth);
		return 0;
	}

	ret = ext4_extents_insert_mapped_block(inode, path, depth, lblock,
					       ext4_extents_fs_blocks_count(sb),
					       out_block, is_new);
	ext4_extents_free_path(path, depth);
	return ret < 0 ? -1 : 0;
}

/*
 * 查询从 lblock 起连续已映射的物理块 run（只读，不分配）。
 * 返回 run 起始物理块与长度 *out_len；未映射返回 0（*out_len=0）。
 */
int ext4_extents_map_blocks(struct inode *inode, uint32_t lblock,
			    uint32_t max_blocks, uint32_t *out_block,
			    uint32_t *out_len)
{
	struct ext4_inode_info *ei;
	struct ext4_extent_header *eh_root;
	struct ext4_ext_path path[EXT4_EXT_MAX_DEPTH + 1];
	struct ext4_extent_header *leaf;
	struct ext4_extent *ex;
	int depth;
	int prev_index;
	int idx;
	uint32_t ee_block;
	uint32_t off_in_extent;
	uint32_t avail;
	uint64_t phys_start;

	if (!inode || !inode->i_private || !out_block || !out_len || max_blocks == 0) {
		return -1;
	}

	*out_block = 0;
	*out_len = 0;

	ei = (struct ext4_inode_info *)inode->i_private;
	eh_root = (struct ext4_extent_header *)ei->i_block;
	if (eh_root->eh_magic != EXT4_EXT_MAGIC) {
		return 0;
	}

	if (ext4_extents_read_path(inode, lblock, path, &depth) < 0) {
		return -1;
	}

	leaf = path[depth].eh;
	idx = ext4_extents_find(leaf, lblock, &ex, &prev_index);
	(void)idx;
	(void)prev_index;
	if (!ex) {
		ext4_extents_free_path(path, depth);
		return 0;
	}

	ee_block = ex->ee_block;
	if (lblock < ee_block) {
		ext4_extents_free_path(path, depth);
		return -1;
	}

	off_in_extent = lblock - ee_block;
	if (off_in_extent >= ex->ee_len) {
		ext4_extents_free_path(path, depth);
		return -1;
	}

	avail = (uint32_t)ex->ee_len - off_in_extent;
	if (avail > max_blocks) {
		avail = max_blocks;
	}
	phys_start = ext4_extent_pblock(ex) + (uint64_t)off_in_extent;
	*out_block = (uint32_t)phys_start;
	*out_len = avail;
	ext4_extents_free_path(path, depth);
	return 0;
}

