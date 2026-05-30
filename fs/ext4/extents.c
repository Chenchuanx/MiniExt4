/*
 * Ext4 简化版 extents 机制
 *
 * 设计说明：
 * - 仅针对普通文件的数据块做 extents 映射；
 * - 每个文件的 extents 列表保存在一个“索引块”中，索引块号存放在 on-disk inode 的 i_block[0] 中；
 * - 支持 Linux ext4 风格的多层 extent tree（inode 内嵌 root + 外部索引/叶子块）；
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
#define EXT4_EXT_MAX_DEPTH 5
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

struct ext4_ext_path {
	struct ext4_extent_header *eh;
	struct ext4_extent_idx *idx;
	uint32_t blocknr;
	char *buf;
};

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
	for (i = 0; i <= depth; i++) {
		if (path[i].buf) {
			free(path[i].buf);
			path[i].buf = (char *)0;
		}
	}
}

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
		struct ext4_ext_path path[EXT4_EXT_MAX_DEPTH + 1];
		int depth;
		struct ext4_extent_header *leaf;
		struct ext4_extent *ex;
		int prev_index;
		int idx;

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
		idx = ext4_extents_find(leaf, lblock, &ex, &prev_index);
		if (idx >= 0 && ex) {
			uint32_t ee_block = ex->ee_block;
			uint64_t phys_start = ext4_extent_pblock(ex);
			uint32_t phys = (uint32_t)(phys_start + (lblock - ee_block));
			*out_block = phys;
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

		{
			struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
			uint32_t preferred_group = (uint32_t)-1;
			uint32_t fs_blocks_count = ext4_get_blocks_count();
			if (sbi && sbi->s_blocks_count > 0 &&
			    (fs_blocks_count == 0 || sbi->s_blocks_count < fs_blocks_count)) {
				fs_blocks_count = sbi->s_blocks_count;
			}
			if (ei) {
				preferred_group = ei->i_alloc_group_hint;
			}
			uint32_t alloc_len = 1;
			uint32_t goal_len = ext4_get_prealloc_goal_len();
			uint32_t new_block = ext4_new_blocks_in_group(sb, goal_len,
							       &alloc_len, preferred_group);
			struct ext4_extent new_ex;
			struct ext4_extent_header *cur_leaf = path[depth].eh;
			int level;
			int insert_pos;
			struct ext4_extent split_right_ex;
			int need_promote = 0;

			if (new_block == 0) {
				ext4_extents_free_path(path, depth);
				return -1;
			}
			if (fs_blocks_count == 0 || new_block >= fs_blocks_count) {
				ext4_extents_free_path(path, depth);
				return -1;
			}

			memset(&new_ex, 0, sizeof(new_ex));
			new_ex.ee_block = lblock;
			if (alloc_len == 0) {
				alloc_len = 1;
			}
			if (alloc_len > 0x7fffU) {
				alloc_len = 0x7fffU;
			}
			if (new_block + alloc_len > fs_blocks_count) {
				alloc_len = fs_blocks_count - new_block;
				if (alloc_len == 0) {
					ext4_extents_free_path(path, depth);
					return -1;
				}
			}
			new_ex.ee_len = (uint16_t)alloc_len;
			ext4_extent_set_pblock(&new_ex, (uint64_t)new_block);
			/* file.c 会对 is_new 记 1 个块，这里补上其余预分配块的 i_blocks 记账。 */
			if (alloc_len > 1) {
				uint32_t sectors_per_block = (block_size + 511U) / 512U;
				inode->i_blocks += (unsigned long)(alloc_len - 1U) *
						   (unsigned long)sectors_per_block;
				inode->i_state |= I_DIRTY;
			}

			ret = ext4_extents_insert(cur_leaf, &new_ex);
			if (ret == 0) {
				if (path[depth].blocknr != 0 &&
				    ext4_write_block(path[depth].blocknr, path[depth].buf) < 0) {
					ext4_extents_free_path(path, depth);
					return -1;
				}
				*out_block = new_block;
				if (is_new) {
					*is_new = 1;
				}
				ext4_extents_free_path(path, depth);
				return 0;
			}

			if (cur_leaf->eh_entries >= cur_leaf->eh_max) {
				struct ext4_extent *leaf_exts = (struct ext4_extent *)(cur_leaf + 1);
				int n = (int)cur_leaf->eh_entries;
				int i;
				int right_count;
				char *right_buf;
				struct ext4_extent_header *right_eh;
				struct ext4_extent *right_exts;
				uint32_t right_blk;

				if (n <= 0 || n != (int)cur_leaf->eh_max) {
					ext4_extents_free_path(path, depth);
					return -1;
				}

				/* 与 Linux ext4 一致：在插入点分裂，而非对半分。 */
				insert_pos = n;
				while (insert_pos > 0 && leaf_exts[insert_pos - 1].ee_block > new_ex.ee_block) {
					insert_pos--;
				}
				right_count = n - insert_pos;

				right_buf = (char *)malloc(block_size);
				if (!right_buf) {
					ext4_extents_free_path(path, depth);
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
					right_exts[0] = new_ex;
					right_eh->eh_entries = 1;
					split_right_ex = new_ex;
				} else {
					leaf_exts[insert_pos] = new_ex;
					cur_leaf->eh_entries = (uint16_t)(insert_pos + 1);
					split_right_ex = right_exts[0];
				}

				right_blk = ext4_new_block(sb);
				if (right_blk == 0) {
					free(right_buf);
					ext4_extents_free_path(path, depth);
					return -1;
				}
				if (right_blk >= fs_blocks_count) {
					free(right_buf);
					ext4_extents_free_path(path, depth);
					return -1;
				}
				if (path[depth].blocknr != 0 &&
				    ext4_write_block(path[depth].blocknr, path[depth].buf) < 0) {
					free(right_buf);
					ext4_extents_free_path(path, depth);
					return -1;
				}
				if (ext4_write_block(right_blk, right_buf) < 0) {
					free(right_buf);
					ext4_extents_free_path(path, depth);
					return -1;
				}
				free(right_buf);

				need_promote = 1;
				for (level = depth - 1; level >= 0 && need_promote; level--) {
					struct ext4_extent_header *ieh = path[level].eh;
					struct ext4_extent_idx new_idx;
					struct ext4_extent_idx *idxs;
					int j;
					uint32_t promote_lblk = split_right_ex.ee_block;
					uint32_t promote_blk = right_blk;

					memset(&new_idx, 0, sizeof(new_idx));
					new_idx.ei_block = promote_lblk;
					ext4_idx_set_pblock(&new_idx, (uint64_t)promote_blk);

					if (ieh->eh_entries < ieh->eh_max) {
						idxs = (struct ext4_extent_idx *)(ieh + 1);
						insert_pos = ieh->eh_entries;
						for (j = 0; j < (int)ieh->eh_entries; j++) {
							if (idxs[j].ei_block > new_idx.ei_block) {
								insert_pos = j;
								break;
							}
						}
						uint16_t ieh_entries = ieh->eh_entries;
						if (ext4_extents_insert_entry(idxs, &ieh_entries, ieh->eh_max,
									      sizeof(struct ext4_extent_idx),
									      insert_pos, &new_idx) < 0) {
							ext4_extents_free_path(path, depth);
							return -1;
						}
						ieh->eh_entries = ieh_entries;
						if (path[level].blocknr != 0 &&
						    ext4_write_block(path[level].blocknr, path[level].buf) < 0) {
							ext4_extents_free_path(path, depth);
							return -1;
						}
						need_promote = 0;
					} else {
						int nidx = (int)ieh->eh_entries;
						int right_count;
						char *right_ibuf;
						struct ext4_extent_header *right_ieh;
						struct ext4_extent_idx *left_arr;
						struct ext4_extent_idx *right_arr;
						uint32_t new_iblk;

						if (nidx <= 0 || nidx != (int)ieh->eh_max) {
							ext4_extents_free_path(path, depth);
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
							ext4_extents_free_path(path, depth);
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
							split_right_ex.ee_block = new_idx.ei_block;
						} else {
							left_arr[insert_pos] = new_idx;
							ieh->eh_entries = (uint16_t)(insert_pos + 1);
							split_right_ex.ee_block = right_arr[0].ei_block;
						}

						new_iblk = ext4_new_block(sb);
						if (new_iblk == 0) {
							free(right_ibuf);
							ext4_extents_free_path(path, depth);
							return -1;
						}
						if (new_iblk >= fs_blocks_count) {
							free(right_ibuf);
							ext4_extents_free_path(path, depth);
							return -1;
						}
						if (path[level].blocknr != 0 &&
						    ext4_write_block(path[level].blocknr, path[level].buf) < 0) {
							free(right_ibuf);
							ext4_extents_free_path(path, depth);
							return -1;
						}
						if (ext4_write_block(new_iblk, right_ibuf) < 0) {
							free(right_ibuf);
							ext4_extents_free_path(path, depth);
							return -1;
						}

						right_blk = new_iblk;
						free(right_ibuf);
						need_promote = 1;
					}
				}

				if (need_promote) {
					struct ext4_extent_header old_root;
					char *old_root_data;
					uint32_t old_root_blk;
					struct ext4_extent_header *new_right_root;
					uint32_t new_right_blk;
					struct ext4_extent_header *root = (struct ext4_extent_header *)ei->i_block;
					struct ext4_extent_idx *root_idx;
					uint32_t old_root_first_lblk;

					if (root->eh_depth >= EXT4_EXT_MAX_DEPTH) {
						ext4_extents_free_path(path, depth);
						return -1;
					}

					old_root = *root;
					old_root_data = (char *)(root + 1);
					if (old_root.eh_entries == 0) {
						ext4_extents_free_path(path, depth);
						return -1;
					}
					old_root_first_lblk = (old_root.eh_depth == 0)
						? ((struct ext4_extent *)old_root_data)[0].ee_block
						: ((struct ext4_extent_idx *)old_root_data)[0].ei_block;
					old_root_blk = ext4_new_block(sb);
					if (old_root_blk == 0) {
						ext4_extents_free_path(path, depth);
						return -1;
					}
					if (old_root_blk >= fs_blocks_count) {
						ext4_extents_free_path(path, depth);
						return -1;
					}
					{
						char *tmpbuf = (char *)malloc(block_size);
						if (!tmpbuf) {
							ext4_extents_free_path(path, depth);
							return -1;
						}
						ext4_extents_init_node((struct ext4_extent_header *)tmpbuf, block_size, old_root.eh_depth);
						((struct ext4_extent_header *)tmpbuf)->eh_entries = old_root.eh_entries;
						memcpy(tmpbuf + sizeof(struct ext4_extent_header),
						       old_root_data,
						       (size_t)old_root.eh_entries *
						       (old_root.eh_depth == 0 ? sizeof(struct ext4_extent)
									       : sizeof(struct ext4_extent_idx)));
						if (ext4_write_block(old_root_blk, tmpbuf) < 0) {
							free(tmpbuf);
							ext4_extents_free_path(path, depth);
							return -1;
						}
						free(tmpbuf);
					}

					new_right_blk = right_blk;
					new_right_root = (struct ext4_extent_header *)ei->i_block;
					ext4_extents_init_node(new_right_root, sizeof(ei->i_block),
							       (uint16_t)(old_root.eh_depth + 1));
					new_right_root->eh_entries = 2;
					root_idx = (struct ext4_extent_idx *)(new_right_root + 1);
					memset(root_idx, 0, sizeof(struct ext4_extent_idx) * 2);
					root_idx[0].ei_block = old_root_first_lblk;
					ext4_idx_set_pblock(&root_idx[0], (uint64_t)old_root_blk);
					root_idx[1].ei_block = split_right_ex.ee_block;
					ext4_idx_set_pblock(&root_idx[1], (uint64_t)new_right_blk);
				}

				*out_block = new_block;
				if (is_new) {
					*is_new = 1;
				}
				ext4_extents_free_path(path, depth);
				return 0;
			}

			ext4_extents_free_path(path, depth);
			return -1;
		}
	}

	return 0;
}

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

