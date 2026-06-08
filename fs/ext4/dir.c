/*
 * Ext4 目录操作
 * 参考 Linux 内核 fs/ext4/namei.c、fs/ext4/dir.c
 * 实现目录项查找、添加、删除与遍历
 */

#include <linux/fs.h>
#include <linux/memory.h>
#include <fs/ext4/ext4.h>
#include <fs/ext4/htree.h>

#include <fs/dentry.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 前向声明 */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern int ext4_write_block(uint32_t blocknr, const void *buf);
extern uint32_t ext4_get_block_size(void);
/* 使用 extents 解析逻辑块 -> 物理块（fs/ext4/extents.c） */
extern int ext4_extents_get_block(struct inode *inode, uint32_t lblock,
				  int create, uint32_t *out_block, int *is_new);
extern uint32_t ext4_new_block(struct super_block *sb);
extern int ext4_free_block(struct super_block *sb, uint32_t blocknr);

/* 简化的内存分配（使用静态池） */
#define MAX_MALLOC BLOCK_SIZE
#define MAX_MALLOC_BLOCKS 8
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

/* 小端序读写辅助（与磁盘格式一致） */
static inline uint32_t le32_to_cpu(uint32_t x) { return x; }
static inline uint16_t le16_to_cpu(uint16_t x) { return x; }
static inline void cpu_to_le32_val(uint32_t *p, uint32_t x) { *p = x; }
static inline void cpu_to_le16_val(uint16_t *p, uint16_t x) { *p = x; }

/* 目录项记录长度（按 4 字节对齐） */
#define EXT4_DIR_REC_LEN(name_len) (((name_len) + 8 + 3) & ~3)

/* 不在 ext4 内维护第二套目录级内存索引：依赖 on-disk HTree + VFS dcache。 */
#define EXT4_DIR_MEM_INDEX_ENABLED 0

/* 前向声明：目录块号获取函数 */
static int ext4_dir_get_blocknr(struct inode *dir, uint32_t lblock,
				uint32_t *out_blocknr);

static void ext4_dir_index_invalidate(struct inode *dir)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	if (!ei)
		return;
	ei->dir_index = (void *)0;
}

/* ext4 内不维护目录级内存索引，函数保留为 no-op，便于调用点保持统一。 */
static void ext4_dir_index_add(struct inode *dir, const struct qstr *name,
			       uint32_t blocknr, uint32_t off)
{
	(void)dir;
	(void)name;
	(void)blocknr;
	(void)off;
}

static int ext4_dir_index_lookup(struct inode *dir, const struct qstr *name,
				 uint32_t *blocknr, uint32_t *off)
{
	(void)dir;
	(void)name;
	(void)blocknr;
	(void)off;
	return -1;
}

/* 统一获取目录的数据块号：
 * - 对于未开启 extents 的 inode，直接使用 i_block[0..11] 作为块号；
 * - 对于开启 EXTENTS 标志的目录，使用 extents 机制解析逻辑块号。
 */
static int ext4_dir_get_blocknr(struct inode *dir, uint32_t lblock,
				uint32_t *out_blocknr)
{
	struct ext4_inode_info *ei;
	uint32_t block_size = ext4_get_block_size();

	if (!dir || !out_blocknr) {
		return -1;
	}

	ei = (struct ext4_inode_info *)dir->i_private;
	if (!ei) {
		return -1;
	}

	/* 开启 EXTENTS：通过 extents 映射逻辑块 -> 物理块 */
	if (ei->i_flags & EXT4_INODE_FLAG_EXTENTS) {
		uint32_t phys = 0;
		int is_new = 0;
		int ret = ext4_extents_get_block(dir, lblock, 0, &phys, &is_new);
		if (ret < 0 || phys == 0) {
			return -1;
		}
		*out_blocknr = phys;
		return 0;
	}

	/* 未开启 EXTENTS：直接块 + 一级间接块。 */
	if (lblock < 12) {
		if (ei->i_block[lblock] == 0) {
			return -1;
		}
		*out_blocknr = ei->i_block[lblock];
		return 0;
	}
	{
		uint32_t idx = lblock - 12;
		uint32_t per_indirect;
		uint32_t iblk;
		char *ibuf;
		uint32_t *ents;
		uint32_t phys;

		if (block_size == 0 || (block_size & 3) != 0) {
			return -1;
		}
		per_indirect = block_size / 4;
		if (idx >= per_indirect) {
			return -1;
		}
		iblk = ei->i_block[12];
		if (iblk == 0) {
			return -1;
		}
		ibuf = (char *)malloc(block_size);
		if (!ibuf) {
			return -1;
		}
		if (ext4_read_block(iblk, ibuf) < 0) {
			free(ibuf);
			return -1;
		}
		ents = (uint32_t *)ibuf;
		phys = le32_to_cpu(ents[idx]);
		free(ibuf);
		if (phys == 0) {
			return -1;
		}
		*out_blocknr = phys;
		return 0;
	}
}

static int ext4_dir_set_blocknr(struct inode *dir, uint32_t lblock, uint32_t phys)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	struct super_block *sb = dir->i_sb;
	uint32_t block_size = ext4_get_block_size();
	uint32_t per_indirect;
	uint32_t idx;
	uint32_t iblk;
	char *ibuf;
	uint32_t *ents;

	if (!ei || !sb || phys == 0) {
		return -1;
	}
	if (lblock < 12) {
		ei->i_block[lblock] = phys;
		return 0;
	}
	if (ei->i_flags & EXT4_INODE_FLAG_EXTENTS) {
		return -1;
	}
	if (block_size == 0 || (block_size & 3) != 0) {
		return -1;
	}
	per_indirect = block_size / 4;
	idx = lblock - 12;
	if (idx >= per_indirect) {
		return -1;
	}

	iblk = ei->i_block[12];
	if (iblk == 0) {
		iblk = ext4_new_block(sb);
		if (iblk == 0) {
			return -1;
		}
		ei->i_block[12] = iblk;
		ibuf = (char *)malloc(block_size);
		if (!ibuf) {
			ei->i_block[12] = 0;
			(void)ext4_free_block(sb, iblk);
			return -1;
		}
		memset(ibuf, 0, block_size);
		/* i_blocks 统计需要包含一级间接块本身。 */
		dir->i_blocks += (block_size / 512);
	} else {
		ibuf = (char *)malloc(block_size);
		if (!ibuf) {
			return -1;
		}
		if (ext4_read_block(iblk, ibuf) < 0) {
			free(ibuf);
			return -1;
		}
	}
	ents = (uint32_t *)ibuf;
	ents[idx] = phys;
	if (ext4_write_block(iblk, ibuf) < 0) {
		free(ibuf);
		return -1;
	}
	free(ibuf);
	return 0;
}

/* HTree 实现在 fs/ext4/htree.c */
struct ext4_dir_leaf_meta {
	uint32_t lblk;
	uint32_t max_hash;
};

static int ext4_dir_leaf_calc_max_hash(const struct inode *dir, const char *leaf,
				       uint32_t block_size, uint32_t *out_max_hash)
{
	uint32_t off = 0;
	uint32_t max_hash = 0;
	int has = 0;
	while (off < block_size) {
		struct ext4_dir_entry *de = (struct ext4_dir_entry *)(leaf + off);
		uint16_t rec = le16_to_cpu(de->rec_len);
		uint16_t nl = (uint16_t)de->name_len;
		uint32_t ino = le32_to_cpu(de->inode);
		if (rec == 0 || (rec & 3) != 0 || off + rec > block_size) return -1;
		if (ino != 0 && nl > 0) {
			struct qstr q;
			uint32_t h;
			q.name = (const unsigned char *)de->name;
			q.len = nl;
			q.hash = 0;
			h = ext4_htree_name_hash32(dir, &q);
			if (!has || h > max_hash) max_hash = h;
			has = 1;
		}
		off += rec;
	}
	*out_max_hash = has ? max_hash : 0;
	return 0;
}

static void ext4_dir_refresh_size_blocks(struct inode *dir)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	uint32_t block_size = ext4_get_block_size();
	uint32_t max_lblocks = 12 + ((block_size && (block_size & 3) == 0) ? (block_size / 4) : 0);
	uint32_t blk_idx;
	uint32_t blocknr;
	uint32_t highest = 0;
	uint32_t data_cnt = 0;
	int has = 0;

	if (!ei || block_size == 0) return;
	for (blk_idx = 0; blk_idx < max_lblocks; blk_idx++) {
		if (ext4_dir_get_blocknr(dir, blk_idx, &blocknr) < 0) break;
		highest = blk_idx;
		data_cnt++;
		has = 1;
	}
	if (has) {
		dir->i_size = (uint64_t)block_size * (uint64_t)(highest + 1);
	} else {
		dir->i_size = 0;
	}
	dir->i_blocks = (uint64_t)(block_size / 512) * (uint64_t)data_cnt;
	if (ei->i_block[12] != 0) dir->i_blocks += (block_size / 512);
}

/* 把当前线性目录重建为 HTree。支持已扩展为多块的线性目录。 */
static int ext4_dir_upgrade_to_htree(struct inode *dir)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	struct super_block *sb = dir->i_sb;
	struct ext4_sb_info *sbi;
	uint32_t block_size = ext4_get_block_size();
	uint32_t max_lblocks;
	uint32_t old_last = 0;
	uint32_t old_cnt = 0;
	uint32_t root_phys = 0;
	uint32_t new_leaf_lblk = 0;
	uint32_t new_leaf_phys = 0;
	uint32_t parent_ino = (uint32_t)dir->i_ino;
	int new_leaf_mapped = 0;
	char *root_old = (char *)0;
	char *root_new = (char *)0;
	char *leaf_buf = (char *)0;
	char *tmp_buf = (char *)0;
	struct ext4_dir_leaf_meta *meta = (struct ext4_dir_leaf_meta *)0;
	uint16_t rec1;
	uint16_t rec2;
	uint16_t dx_limit;
	uint32_t meta_n;
	uint32_t i;
	int ret = -1;

	if (!ei || !sb) return -1;
	if (ei->i_flags & (EXT4_INODE_FLAG_INDEX | EXT4_INODE_FLAG_EXTENTS)) return -1;
	if (block_size == 0 || (block_size & 3) != 0) return -1;
	sbi = (struct ext4_sb_info *)sb->s_fs_info;
	max_lblocks = 12 + (block_size / 4);

	for (i = 0; i < max_lblocks; i++) {
		uint32_t b;
		if (ext4_dir_get_blocknr(dir, i, &b) < 0) break;
		old_last = i;
		old_cnt++;
	}
	if (old_cnt == 0) return -1;
	if (ext4_dir_get_blocknr(dir, 0, &root_phys) < 0) return -1;

	new_leaf_lblk = old_last + 1;
	if (new_leaf_lblk >= max_lblocks) return -1;
	new_leaf_phys = ext4_new_block(sb);
	if (new_leaf_phys == 0) return -1;
	if (ext4_dir_set_blocknr(dir, new_leaf_lblk, new_leaf_phys) < 0) {
		(void)ext4_free_block(sb, new_leaf_phys);
		return -1;
	}
	new_leaf_mapped = 1;

	root_old = (char *)malloc(block_size);
	root_new = (char *)malloc(block_size);
	leaf_buf = (char *)malloc(block_size);
	tmp_buf = (char *)malloc(block_size);
	if (!root_old || !root_new || !leaf_buf || !tmp_buf) goto out;
	if (ext4_read_block(root_phys, root_old) < 0) goto out;
	{
		struct ext4_dir_entry *dotdot = (struct ext4_dir_entry *)(root_old + EXT4_DIR_REC_LEN(1));
		if (le16_to_cpu(dotdot->rec_len) >= EXT4_DIR_REC_LEN(2)) {
			parent_ino = le32_to_cpu(dotdot->inode);
		}
	}

	memset(leaf_buf, 0, block_size);
	{
		struct ext4_dir_entry *tail = (struct ext4_dir_entry *)leaf_buf;
		tail->inode = 0;
		tail->rec_len = (uint16_t)block_size;
		tail->name_len = 0;
		tail->file_type = 0;
	}
	/* 把 block0 中除 "."/".." 外的目录项迁移到新叶子块。 */
	{
		uint32_t off = 0;
		uint32_t de_off = 0;
		while (off < block_size) {
			struct ext4_dir_entry *de = (struct ext4_dir_entry *)(root_old + off);
			uint16_t rec = le16_to_cpu(de->rec_len);
			uint16_t nl = (uint16_t)de->name_len;
			uint32_t ino = le32_to_cpu(de->inode);
			int is_dot = 0;
			struct qstr q;
			if (rec == 0 || (rec & 3) != 0 || off + rec > block_size) goto out;
			if (ino != 0 && nl > 0) {
				if (nl == 1 && de->name[0] == '.') is_dot = 1;
				if (nl == 2 && de->name[0] == '.' && de->name[1] == '.') is_dot = 1;
				if (!is_dot) {
					q.name = (const unsigned char *)de->name;
					q.len = nl;
					q.hash = 0;
					if (ext4_htree_try_insert_leaf(leaf_buf, block_size, &q, ino,
								      (uint16_t)EXT4_DIR_REC_LEN((int)nl), &de_off) < 0) {
						goto out;
					}
				}
			}
			off += rec;
		}
	}
	if (ext4_write_block(new_leaf_phys, leaf_buf) < 0) goto out;

	/* 统计叶子集合：旧 block1..blockN + 新迁移叶子。 */
	meta_n = old_last + 1; /* 去掉 root(0) 后再 +1 个新叶子 */
	meta = (struct ext4_dir_leaf_meta *)malloc(sizeof(*meta) * meta_n);
	if (!meta) goto out;
	for (i = 0; i < old_last; i++) {
		uint32_t lblk = i + 1;
		uint32_t phys;
		if (ext4_dir_get_blocknr(dir, lblk, &phys) < 0) goto out;
		if (ext4_read_block(phys, tmp_buf) < 0) goto out;
		if (ext4_dir_leaf_calc_max_hash(dir, tmp_buf, block_size, &meta[i].max_hash) < 0) goto out;
		meta[i].lblk = lblk;
	}
	meta[meta_n - 1].lblk = new_leaf_lblk;
	if (ext4_read_block(new_leaf_phys, tmp_buf) < 0) goto out;
	if (ext4_dir_leaf_calc_max_hash(dir, tmp_buf, block_size, &meta[meta_n - 1].max_hash) < 0) goto out;

	/* 按最大哈希升序排列叶子。 */
	for (i = 0; i + 1 < meta_n; i++) {
		uint32_t j;
		for (j = i + 1; j < meta_n; j++) {
			if (meta[i].max_hash > meta[j].max_hash) {
				struct ext4_dir_leaf_meta t = meta[i];
				meta[i] = meta[j];
				meta[j] = t;
			}
		}
	}

	/* 重写 block0 为 HTree root。 */
	memset(root_new, 0, block_size);
	{
		struct ext4_dir_entry *de = (struct ext4_dir_entry *)root_new;
		struct ext4_dx_root_info *dx_info;
		struct ext4_dx_countlimit *dx_cl;
		struct ext4_dx_entry *dx_entries;

		rec1 = (uint16_t)EXT4_DIR_REC_LEN(1);
		de->inode = (uint32_t)dir->i_ino;
		de->rec_len = rec1;
		de->name_len = 1;
		de->file_type = 2;
		de->name[0] = '.';

		de = (struct ext4_dir_entry *)(root_new + rec1);
		rec2 = (uint16_t)(block_size - rec1);
		de->inode = parent_ino;
		de->rec_len = rec2;
		de->name_len = 2;
		de->file_type = 2;
		de->name[0] = '.';
		de->name[1] = '.';

		dx_info = (struct ext4_dx_root_info *)(root_new + rec1 + EXT4_DIR_REC_LEN(2));
		dx_cl = (struct ext4_dx_countlimit *)(dx_info + 1);
		dx_entries = (struct ext4_dx_entry *)dx_cl;
		dx_limit = (uint16_t)((block_size -
				      (uint16_t)(rec1 + EXT4_DIR_REC_LEN(2) +
						 sizeof(struct ext4_dx_root_info))) /
				     (uint16_t)sizeof(struct ext4_dx_entry));
		if (meta_n == 0 || meta_n > dx_limit) goto out;

		dx_info->reserved_zero = 0;
		dx_info->hash_version = (uint8_t)(sbi ? sbi->s_def_hash_version : EXT4_DX_HASH_LEGACY);
		dx_info->info_length = (uint8_t)sizeof(struct ext4_dx_root_info);
		dx_info->indirect_levels = 0;
		dx_info->unused_flags = 0;
		dx_cl->limit = dx_limit;
		dx_cl->count = (uint16_t)meta_n;
		for (i = 0; i < meta_n; i++) {
			dx_entries[i].block = meta[i].lblk;
			if (i > 0) {
				uint32_t h = meta[i - 1].max_hash;
				if (h != 0xffffffffU) h += 1;
				dx_entries[i].hash = h;
			}
		}
	}
	if (ext4_write_block(root_phys, root_new) < 0) goto out;

	ei->i_flags |= EXT4_INODE_FLAG_INDEX;
	ext4_dir_refresh_size_blocks(dir);
	if (dir->i_sb && dir->i_sb->s_op && dir->i_sb->s_op->write_inode) {
		(void)dir->i_sb->s_op->write_inode(dir, (struct writeback_control *)0);
	}
	ext4_dir_index_invalidate(dir);
	ret = 0;

out:
	if (ret < 0) {
		/* 升级失败时尽量回滚新增叶子映射。 */
		uint32_t iblk = ei ? ei->i_block[12] : 0;
		if (new_leaf_mapped && ei && new_leaf_lblk < 12) ei->i_block[new_leaf_lblk] = 0;
		if (new_leaf_mapped && iblk != 0 && new_leaf_lblk >= 12) {
			char *ib = (char *)malloc(block_size);
			if (ib && ext4_read_block(iblk, ib) == 0) {
				uint32_t *ents = (uint32_t *)ib;
				uint32_t idx = new_leaf_lblk - 12;
				if (idx < (block_size / 4)) {
					ents[idx] = 0;
					(void)ext4_write_block(iblk, ib);
				}
			}
			if (ib) free(ib);
		}
		if (new_leaf_phys != 0) (void)ext4_free_block(sb, new_leaf_phys);
	}
	if (root_old) free(root_old);
	if (root_new) free(root_new);
	if (leaf_buf) free(leaf_buf);
	if (tmp_buf) free(tmp_buf);
	if (meta) free(meta);
	return ret;
}

/**
 * ext4_find_entry - 在目录中按名称查找目录项
 * @dir: 目录 inode（VFS）
 * @name: 要查找的名称（qstr）
 * @out_ino: 成功时返回该目录项的 inode 号
 * @out_blocknr: 成功时返回包含该目录项的块号
 * @out_off: 该目录项在块内的字节偏移
 *
 * 成功返回 0，未找到或错误返回 -1。
 */
int ext4_find_entry(struct inode *dir, const struct qstr *name,
		    unsigned long *out_ino, uint32_t *out_blocknr, uint32_t *out_off)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	struct super_block *sb = dir->i_sb;
	uint32_t block_size = ext4_get_block_size();
	char *buf;
	uint32_t off;
	uint32_t blk_idx;
	int ret;

	if (!ei || !name || name->len == 0 || name->len > 255) {
		return -1;
	}

	if (ei->i_flags & EXT4_INODE_FLAG_INDEX) {
		uint32_t root_phys, leaf_lblk, leaf_phys;
		uint32_t h32;
		char *buf_root = (char *)malloc(block_size);
		if (!buf_root) return -1;
		if (ext4_dir_get_blocknr(dir, 0, &root_phys) == 0 &&
		    ext4_read_block(root_phys, buf_root) == 0) {
			h32 = ext4_htree_name_hash32(dir, name);
			if (ext4_htree_pick_leaf_from_root(dir, block_size, buf_root, h32, &leaf_lblk) == 0 &&
			    ext4_dir_get_blocknr(dir, leaf_lblk, &leaf_phys) == 0) {
				free(buf_root);
				buf_root = (char *)0;
				buf = (char *)malloc(block_size);
				if (!buf) return -1;
				if (ext4_read_block(leaf_phys, buf) == 0) {
					off = 0;
					while (off < block_size) {
						struct ext4_dir_entry *de = (struct ext4_dir_entry *)(buf + off);
						uint16_t rec_len = le16_to_cpu(de->rec_len);
						uint16_t name_len = (uint16_t)de->name_len;
						uint32_t ino = le32_to_cpu(de->inode);
						if (rec_len == 0) break;
						if (ino != 0 && name_len == (uint16_t)name->len &&
						    name->name && memcmp(de->name, name->name, (size_t)name_len) == 0) {
							*out_ino = (unsigned long)ino;
							*out_blocknr = leaf_phys;
							*out_off = off;
							ext4_dir_index_add(dir, name, leaf_phys, off);
							free(buf);
							return 0;
						}
						off += rec_len;
					}
				}
				free(buf);
			} else {
				free(buf_root);
			}
		} else {
			free(buf_root);
		}
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}

	/* 优先尝试使用目录索引（B+Tree）快速定位块与偏移 */
	if (ext4_dir_index_lookup(dir, name, out_blocknr, out_off) == 0) {
		struct ext4_dir_entry *de;
		uint16_t rec_len;
		uint16_t name_len;
		uint32_t ino;

		ret = ext4_read_block(*out_blocknr, buf);
		if (ret < 0) {
			free(buf);
			return -1;
		}

		de = (struct ext4_dir_entry *)(buf + *out_off);
		rec_len = le16_to_cpu(de->rec_len);
		name_len = (uint16_t)de->name_len;
		ino = le32_to_cpu(de->inode);

		if (rec_len != 0 && ino != 0 &&
		    name_len == (uint16_t)name->len &&
		    name->name && memcmp(de->name, name->name, (size_t)name_len) == 0) {
			*out_ino = (unsigned long)ino;
			free(buf);
			return 0;
		}
		/* 索引失效则退回到全量扫描 */
	}

	/* 遍历目录的逻辑块（对 extents/非 extents 统一处理） */
	for (blk_idx = 0; ; blk_idx++) {
		uint32_t blocknr;

		if (ext4_dir_get_blocknr(dir, blk_idx, &blocknr) < 0) {
			break;
		}
		ret = ext4_read_block(blocknr, buf);
		if (ret < 0) {
			free(buf);
			return -1;
		}
		off = 0;
		while (off < block_size) {
			struct ext4_dir_entry *de = (struct ext4_dir_entry *)(buf + off);
			uint16_t rec_len = le16_to_cpu(de->rec_len);
			uint16_t name_len = le16_to_cpu(de->name_len);
			uint32_t ino = le32_to_cpu(de->inode);

			if (rec_len == 0) {
				free(buf);
				return -1;
			}
			if (ino != 0 && name_len == (uint16_t)name->len &&
			    name->name && memcmp(de->name, name->name, (size_t)name_len) == 0) {
				*out_ino = (unsigned long)ino;
				*out_blocknr = blocknr;
				*out_off = off;
				/* 命中时把结果写入索引，便于后续快速查找 */
				ext4_dir_index_add(dir, name, blocknr, off);
				free(buf);
				return 0;
			}
			off += rec_len;
		}
	}

	free(buf);
	return -1;
}

/**
 * ext4_add_entry - 在目录中新增一条目录项
 * @dir: 目录 inode
 * @name: 名称（qstr）
 * @ino: 新目录项对应的 inode 号
 *
 * 在目录块中寻找空闲空间并插入目录项。成功返回 0，失败（如无空间）返回 -1。
 */
int ext4_add_entry(struct inode *dir, const struct qstr *name, unsigned long ino)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	struct super_block *sb = dir->i_sb;
	uint32_t block_size = ext4_get_block_size();
	uint32_t max_lblocks;
	uint16_t rec_len = EXT4_DIR_REC_LEN(name->len);
	char *buf;
	uint32_t blk_idx;
	uint32_t off;
	int ret;

	if (!ei || !name || name->len == 0 || name->len > 255) {
		return -1;
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}

	/* HTree 单层多叶子：根块 0 为索引，按 hash 选逻辑块号，叶满则分裂并扩展 dx_entry */
	if (ei->i_flags & EXT4_INODE_FLAG_INDEX) {
		char *buf_root;
		char *buf_leaf;
		uint32_t h32, leaf_lblk, root_phys, leaf_phys;
		uint32_t de_off;

		buf_root = (char *)malloc(block_size);
		buf_leaf = (char *)malloc(block_size);
		if (!buf_root || !buf_leaf) {
			if (buf_root) free(buf_root);
			if (buf_leaf) free(buf_leaf);
			free(buf);
			return -1;
		}
		if (ext4_dir_get_blocknr(dir, 0, &root_phys) < 0 ||
		    ext4_read_block(root_phys, buf_root) < 0) {
			free(buf_root);
			free(buf_leaf);
			free(buf);
			return -1;
		}

		h32 = ext4_htree_name_hash32(dir, name);
		if (ext4_htree_pick_leaf_from_root(dir, block_size, buf_root, h32, &leaf_lblk) < 0) {
			free(buf_root);
			free(buf_leaf);
			free(buf);
			return -1;
		}
		if (ext4_dir_get_blocknr(dir, leaf_lblk, &leaf_phys) < 0) {
			free(buf_root);
			free(buf_leaf);
			free(buf);
			return -1;
		}
		if (ext4_read_block(leaf_phys, buf_leaf) < 0) {
			free(buf_root);
			free(buf_leaf);
			free(buf);
			return -1;
		}

		if (ext4_htree_try_insert_leaf(buf_leaf, block_size, name, ino, rec_len, &de_off) == 0) {
			ret = ext4_write_block(leaf_phys, buf_leaf);
			if (ret >= 0)
				ext4_dir_index_add(dir, name, leaf_phys, de_off);
			free(buf_root);
			free(buf_leaf);
			free(buf);
			return ret < 0 ? -1 : 0;
		}

		/* 当前叶放不下：分裂并已在分裂路径中插入新目录项 */
		ret = ext4_htree_split_leaf(dir, sb, ei, block_size, buf_root, root_phys,
					    leaf_lblk, name, ino, rec_len);
		free(buf_root);
		free(buf_leaf);
		free(buf);
		if (ret == 0)
			ext4_dir_index_invalidate(dir);
		return ret < 0 ? -1 : 0;
	}

	/* 非 HTree 目录：沿用原来的线性扫描逻辑 */
	if (ei->i_flags & EXT4_INODE_FLAG_EXTENTS) {
		max_lblocks = 12;
	} else {
		max_lblocks = 12 + (block_size / 4);
	}
	for (blk_idx = 0; blk_idx < max_lblocks; blk_idx++) {
		uint32_t blocknr = 0;
		if (ext4_dir_get_blocknr(dir, blk_idx, &blocknr) < 0) {
			struct ext4_dir_entry *free_de;
			int allocated = 0;

			/* Linux 类似策略：线性目录扩容前优先尝试转为索引目录。 */
			if (!(ei->i_flags & (EXT4_INODE_FLAG_INDEX | EXT4_INODE_FLAG_EXTENTS)) &&
			    blk_idx >= 1) {
				if (ext4_dir_upgrade_to_htree(dir) == 0) {
					free(buf);
					return ext4_add_entry(dir, name, ino);
				}
			}

			/*
			 * 目录块不足时在线扩容：
			 * - extents 目录：通过 extents 映射创建逻辑块；
			 * - 传统目录：分配直接块并填到 i_block[]。
			 */
			if (ei->i_flags & EXT4_INODE_FLAG_EXTENTS) {
				int is_new = 0;
				if (ext4_extents_get_block(dir, blk_idx, 1, &blocknr, &is_new) < 0 ||
				    blocknr == 0) {
					free(buf);
					return -1;
				}
				allocated = is_new ? 1 : 0;
			} else {
				blocknr = ext4_new_block(sb);
				if (blocknr == 0) {
					free(buf);
					return -1;
				}
				if (ext4_dir_set_blocknr(dir, blk_idx, blocknr) < 0) {
					(void)ext4_free_block(sb, blocknr);
					free(buf);
					return -1;
				}
				allocated = 1;
			}

			if (allocated) {
				memset(buf, 0, block_size);
				free_de = (struct ext4_dir_entry *)buf;
				free_de->inode = 0;
				free_de->rec_len = (uint16_t)block_size;
				free_de->name_len = 0;
				free_de->file_type = 0;

				ret = ext4_write_block(blocknr, buf);
				if (ret < 0) {
					(void)ext4_free_block(sb, blocknr);
					free(buf);
					return -1;
				}

				dir->i_size += block_size;
				dir->i_blocks += (block_size / 512);
				if (dir->i_sb && dir->i_sb->s_op && dir->i_sb->s_op->write_inode) {
					(void)dir->i_sb->s_op->write_inode(dir, (struct writeback_control *)0);
				}
			}
		}
		ret = ext4_read_block(blocknr, buf);
		if (ret < 0) {
			free(buf);
			return -1;
		}
		off = 0;
		while (off + rec_len <= block_size) {
			struct ext4_dir_entry *de = (struct ext4_dir_entry *)(buf + off);
			uint16_t d_rec_len = le16_to_cpu(de->rec_len);
			uint16_t d_name_len = (uint16_t)de->name_len;
			uint32_t d_ino = le32_to_cpu(de->inode);
			uint16_t used_len = 0;

			if (d_rec_len == 0 || (d_rec_len & 3) != 0 || off + d_rec_len > block_size) {
				free(buf);
				return -1;
			}
			if (d_ino != 0) {
				used_len = EXT4_DIR_REC_LEN((int)d_name_len);
				if (used_len > d_rec_len) {
					free(buf);
					return -1;
				}
			}

			if (d_rec_len >= (uint16_t)(used_len + rec_len)) {
				struct ext4_dir_entry *new_de;
				uint16_t old_rec = d_rec_len;
				if (d_ino != 0) {
					de->rec_len = used_len;
					new_de = (struct ext4_dir_entry *)((char *)de + used_len);
					new_de->rec_len = (uint16_t)(old_rec - used_len);
				} else {
					new_de = de;
					new_de->rec_len = old_rec;
				}
				new_de->inode = (uint32_t)ino;
				new_de->name_len = (__u8)name->len;
				new_de->file_type = 0;
				if (name->name && name->len > 0)
					memcpy(new_de->name, name->name, (size_t)name->len);
				ret = ext4_write_block(blocknr, buf);
				if (ret >= 0) {
					ext4_dir_index_add(dir, name, blocknr,
							   (uint32_t)((char *)new_de - buf));
				}
				free(buf);
				return ret < 0 ? -1 : 0;
			}
			off += d_rec_len;
		}
	}

	free(buf);
	return -1;
}

/**
 * ext4_remove_entry - 按名称删除目录中的一条目录项
 * @dir: 目录 inode
 * @name: 要删除的名称
 *
 * 将该项与前一项合并（增大前一项的 rec_len）。成功返回 0，未找到或错误返回 -1。
 */
int ext4_remove_entry(struct inode *dir, const struct qstr *name)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	uint32_t block_size = ext4_get_block_size();
	char *buf;
	uint32_t blk_idx;
	uint32_t off;
	uint32_t prev_blocknr;
	uint32_t prev_off;
	int ret;
	/* 必须按块重置：HTree 目录中叶子块首条目的“前项”在上一逻辑块，不能跨块合并 */
	int block_first;

	if (!ei || !name || name->len == 0) {
		return -1;
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}

	prev_blocknr = 0;
	prev_off = 0;

	for (blk_idx = 0; ; blk_idx++) {
		uint32_t blocknr;

		if (ext4_dir_get_blocknr(dir, blk_idx, &blocknr) < 0) {
			break;
		}
		ret = ext4_read_block(blocknr, buf);
		if (ret < 0) {
			free(buf);
			return -1;
		}
		block_first = 1;
		off = 0;
		while (off < block_size) {
			struct ext4_dir_entry *de = (struct ext4_dir_entry *)(buf + off);
			uint16_t rec_len = le16_to_cpu(de->rec_len);
			uint16_t name_len = (uint16_t)de->name_len; /* on-disk: 1 byte */
			uint32_t ino = le32_to_cpu(de->inode);

			if (rec_len == 0) {
				free(buf);
				return -1;
			}
			if (ino != 0 && name_len == (uint16_t)name->len &&
			    name->name && memcmp(de->name, name->name, (size_t)name_len) == 0) {
				/* 找到：与前一项合并或清空 inode（仅同一块内可合并） */
				if (block_first) {
					/* 块内首项：仅将 inode 置 0 */
					de->inode = 0;
					de->name_len = 0;
					de->file_type = 0;
					ret = ext4_write_block(blocknr, buf);
				} else {
					struct ext4_dir_entry *pde;
					uint16_t prev_rec;

					if (prev_blocknr != blocknr) {
						free(buf);
						return -1;
					}
					pde = (struct ext4_dir_entry *)(buf + prev_off);
					prev_rec = le16_to_cpu(pde->rec_len);
					pde->rec_len = (uint16_t)(prev_rec + rec_len);
					ret = ext4_write_block(blocknr, buf);
				}
				if (ret >= 0)
					ext4_dir_index_invalidate(dir);
				free(buf);
				return ret < 0 ? -1 : 0;
			}
			block_first = 0;
			prev_blocknr = blocknr;
			prev_off = off;
			off += rec_len;
		}
	}

	free(buf);
	return -1;
}

/**
 * ext4_dir_foreach - 遍历目录项并对每一项调用回调
 * @dir: 目录 inode
 * @ctx: 传给 filldir 的不透明指针
 * @filldir: 回调(name_len, name, ino, type)，返回非 0 表示停止
 *
 * 成功返回 0，错误返回 -1。
 */
int ext4_dir_foreach(struct inode *dir, void *ctx,
		     int (*filldir)(void *ctx, const char *name, int name_len,
				   unsigned long ino, unsigned int type))
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	uint32_t block_size = ext4_get_block_size();
	char *buf;
	uint32_t blk_idx;
	uint32_t off;
	int ret;

	if (!ei || !filldir) {
		return -1;
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}

	for (blk_idx = 0; ; blk_idx++) {
		uint32_t blocknr;

		if (ext4_dir_get_blocknr(dir, blk_idx, &blocknr) < 0) {
			break;
		}
		ret = ext4_read_block(blocknr, buf);
		if (ret < 0) {
			free(buf);
			return -1;
		}
		off = 0;
		while (off < block_size) {
			struct ext4_dir_entry *de = (struct ext4_dir_entry *)(buf + off);
			uint16_t rec_len = le16_to_cpu(de->rec_len);
			uint16_t name_len = (uint16_t)de->name_len; /* on-disk: 1 byte */
			uint32_t ino = le32_to_cpu(de->inode);

			if (rec_len == 0) {
				free(buf);
				return -1;
			}
			if (ino != 0 && name_len > 0) {
				/* 简化实现：类型传 0（DT_UNKNOWN） */
				int r = filldir(ctx, de->name, name_len, ino, 0);
				if (r != 0) {
					free(buf);
					return 0;
				}
			}
			off += rec_len;
		}
	}

	free(buf);
	return 0;
}
