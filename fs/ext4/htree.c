/*
 * Ext4 HTree helper implementation
 * 把目录 HTree 路由/分裂逻辑从 dir.c 中拆分出来，便于后续扩展多层。
 */

#include <linux/fs.h>
#include <linux/memory.h>
#include <fs/ext4/ext4.h>
#include <fs/ext4/htree.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

extern int ext4_read_block(uint32_t blocknr, void *buf);
extern int ext4_write_block(uint32_t blocknr, const void *buf);
extern uint32_t ext4_new_block(struct super_block *sb);
extern int ext4_free_block(struct super_block *sb, uint32_t blocknr);
extern uint32_t ext4_get_block_size(void);
extern int ext4_extents_get_block(struct inode *inode, uint32_t lblock,
				  int create, uint32_t *out_block, int *is_new);

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

static inline uint32_t le32_to_cpu(uint32_t x) { return x; }
static inline uint16_t le16_to_cpu(uint16_t x) { return x; }

#define EXT4_DIR_REC_LEN(name_len) (((name_len) + 8 + 3) & ~3)
#define EXT4_HTREE_SPLIT_MAX_NAMES 512

struct ext4_htree_leaf_item {
	uint32_t hash;
	uint32_t ino;
	uint8_t name_len;
	char name[255];
};

/* 分裂过程使用共享缓冲，避免在栈上放大数组导致大目录场景不稳定。 */
static struct ext4_htree_leaf_item ext4_htree_split_items[EXT4_HTREE_SPLIT_MAX_NAMES];

static uint32_t dx_hack_hash_signed(const char *name, int len)
{
	uint32_t hash, hash0 = 0x12a3fe2dU, hash1 = 0x37abe8f9U;
	const signed char *scp = (const signed char *)name;
	while (len--) {
		hash = hash1 + (hash0 ^ (((int)*scp++) * 7152373));
		if (hash & 0x80000000U) hash -= 0x7fffffffU;
		hash1 = hash0;
		hash0 = hash;
	}
	return hash0 << 1;
}

static uint32_t dx_hack_hash_unsigned(const char *name, int len)
{
	uint32_t hash, hash0 = 0x12a3fe2dU, hash1 = 0x37abe8f9U;
	const unsigned char *ucp = (const unsigned char *)name;
	while (len--) {
		hash = hash1 + (hash0 ^ (((int)*ucp++) * 7152373));
		if (hash & 0x80000000U) hash -= 0x7fffffffU;
		hash1 = hash0;
		hash0 = hash;
	}
	return hash0 << 1;
}

static int ext4_dir_get_blocknr(struct inode *dir, uint32_t lblock,
				uint32_t *out_blocknr)
{
	struct ext4_inode_info *ei;
	uint32_t block_size = ext4_get_block_size();

	if (!dir || !out_blocknr) return -1;
	ei = (struct ext4_inode_info *)dir->i_private;
	if (!ei) return -1;

	if (ei->i_flags & EXT4_INODE_FLAG_EXTENTS) {
		uint32_t phys = 0;
		int is_new = 0;
		int ret = ext4_extents_get_block(dir, lblock, 0, &phys, &is_new);
		if (ret < 0 || phys == 0) return -1;
		*out_blocknr = phys;
		return 0;
	}
	if (lblock < 12) {
		if (ei->i_block[lblock] == 0) return -1;
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

		if (block_size == 0 || (block_size & 3) != 0) return -1;
		per_indirect = block_size / 4;
		if (idx >= per_indirect) return -1;
		iblk = ei->i_block[12];
		if (iblk == 0) return -1;
		ibuf = (char *)malloc(block_size);
		if (!ibuf) return -1;
		if (ext4_read_block(iblk, ibuf) < 0) { free(ibuf); return -1; }
		ents = (uint32_t *)ibuf;
		phys = le32_to_cpu(ents[idx]);
		free(ibuf);
		if (phys == 0) return -1;
		*out_blocknr = phys;
		return 0;
	}
}

uint32_t ext4_htree_name_hash32(const struct inode *dir, const struct qstr *name)
{
	const struct ext4_sb_info *sbi;
	uint32_t hash;
	uint8_t ver;

	if (!name || !name->name) return 0;
	sbi = (const struct ext4_sb_info *)0;
	if (dir && dir->i_sb) {
		sbi = (const struct ext4_sb_info *)(dir->i_sb->s_fs_info);
	}
	ver = sbi ? sbi->s_def_hash_version : EXT4_DX_HASH_LEGACY;
	switch (ver) {
	case EXT4_DX_HASH_LEGACY_UNSIGNED:
		hash = dx_hack_hash_unsigned((const char *)name->name, (int)name->len);
		break;
	case EXT4_DX_HASH_LEGACY:
	default:
		hash = dx_hack_hash_signed((const char *)name->name, (int)name->len);
		break;
	}
	hash &= ~1U;
	if (hash == 0xffffffffU) hash = 0xfffffffeU;
	return hash;
}

static int ext4_htree_parse_root(const char *root, uint32_t block_size,
				 struct ext4_dx_root_info **out_info,
				 struct ext4_dx_entry **out_entries,
				 int *out_cap, int *out_count)
{
	struct ext4_dir_entry *dot = (struct ext4_dir_entry *)root;
	uint16_t r1 = le16_to_cpu(dot->rec_len);
	struct ext4_dir_entry *dotdot = (struct ext4_dir_entry *)(root + r1);
	uint16_t r2 = le16_to_cpu(dotdot->rec_len);
	uint16_t dotdot_min = EXT4_DIR_REC_LEN((int)dotdot->name_len);
	char *dotdot_end = (char *)dotdot + r2;
	struct ext4_dx_root_info *info;
	struct ext4_dx_countlimit *cl;
	struct ext4_dx_entry *entries;
	int cap, nent;

	if (r1 == 0 || r2 == 0) return -1;
	if ((uint32_t)r1 + (uint32_t)r2 > block_size) return -1;
	if (r2 < dotdot_min) return -1;
	info = (struct ext4_dx_root_info *)((char *)dotdot + dotdot_min);
	if ((char *)(info + 1) > dotdot_end) return -1;
	cl = (struct ext4_dx_countlimit *)(info + 1);
	entries = (struct ext4_dx_entry *)cl;
	if ((char *)(entries + 1) > dotdot_end) return -1;
	cap = (int)le16_to_cpu(cl->limit);
	nent = (int)le16_to_cpu(cl->count);
	if (cap < 1 || nent < 1 || nent > cap) return -1;
	if ((char *)(entries + cap) > dotdot_end) return -1;
	if (info->info_length != sizeof(struct ext4_dx_root_info))
		return -1;
	*out_info = info;
	*out_entries = entries;
	*out_cap = cap;
	*out_count = nent;
	return 0;
}

static int ext4_htree_parse_node(char *node, uint32_t block_size,
				 struct ext4_dx_entry **out_entries,
				 int *out_cap, int *out_count)
{
	struct ext4_dx_node *dn = (struct ext4_dx_node *)node;
	struct ext4_dx_countlimit *cl = &dn->countlimit;
	struct ext4_dx_entry *entries = (struct ext4_dx_entry *)cl;
	int cap, nent;

	(void)block_size;
	cap = (int)le16_to_cpu(cl->limit);
	nent = (int)le16_to_cpu(cl->count);
	if (cap < 1 || nent < 1 || nent > cap) return -1;
	if ((char *)(entries + cap) > node + block_size) return -1;
	*out_entries = entries;
	*out_cap = cap;
	*out_count = nent;
	return 0;
}

static uint32_t ext4_htree_pick_leaf_lblock(struct ext4_dx_entry *entries, int n,
					    uint32_t h32)
{
	int i;
	uint32_t chosen;
	if (n <= 0) return 0;
	chosen = le32_to_cpu(entries[0].block);
	for (i = 1; i < n; i++) {
		if (h32 < le32_to_cpu(entries[i].hash))
			break;
		chosen = le32_to_cpu(entries[i].block);
	}
	return chosen;
}

static int ext4_htree_find_dx_index_for_lblock(struct ext4_dx_entry *entries, int n,
					       uint32_t lblk)
{
	int i;
	for (i = 0; i < n; i++) if (le32_to_cpu(entries[i].block) == lblk) return i;
	return -1;
}

static int ext4_htree_alloc_lblock(struct super_block *sb, struct ext4_inode_info *ei,
				   uint32_t phys, int start_lblk, int *out_lblk)
{
	int j;
	uint32_t block_size;
	uint32_t per_indirect;
	uint32_t iblk;
	char *ibuf;
	uint32_t *ents;
	uint32_t idx_start, idx;
	if (!sb || !ei || !out_lblk || phys == 0) return -1;
	for (j = start_lblk; j < 12; j++) {
		if (ei->i_block[j] == 0) { ei->i_block[j] = phys; *out_lblk = j; return 0; }
	}
	block_size = ext4_get_block_size();
	if (block_size == 0 || (block_size & 3) != 0) return -1;
	per_indirect = block_size / 4;
	idx_start = (start_lblk > 12) ? (uint32_t)(start_lblk - 12) : 0;
	if (idx_start >= per_indirect) return -1;

	iblk = ei->i_block[12];
	if (iblk == 0) {
		iblk = ext4_new_block(sb);
		if (iblk == 0) return -1;
		ei->i_block[12] = iblk;
		ibuf = (char *)malloc(block_size);
		if (!ibuf) { ei->i_block[12] = 0; (void)ext4_free_block(sb, iblk); return -1; }
		memset(ibuf, 0, block_size);
	} else {
		ibuf = (char *)malloc(block_size);
		if (!ibuf) return -1;
		if (ext4_read_block(iblk, ibuf) < 0) { free(ibuf); return -1; }
	}
	ents = (uint32_t *)ibuf;
	for (idx = idx_start; idx < per_indirect; idx++) {
		if (le32_to_cpu(ents[idx]) == 0) {
			ents[idx] = phys;
			if (ext4_write_block(iblk, ibuf) < 0) { free(ibuf); return -1; }
			*out_lblk = (int)(idx + 12);
			free(ibuf);
			return 0;
		}
	}
	free(ibuf);
	return -1;
}

static int ext4_htree_unmap_lblock(struct super_block *sb, struct ext4_inode_info *ei,
				   int lblk)
{
	uint32_t block_size;
	uint32_t per_indirect;
	uint32_t idx;
	uint32_t iblk;
	char *ibuf;
	uint32_t *ents;
	if (!sb || !ei || lblk < 0) return -1;
	if (lblk < 12) {
		ei->i_block[lblk] = 0;
		return 0;
	}
	block_size = ext4_get_block_size();
	if (block_size == 0 || (block_size & 3) != 0) return -1;
	per_indirect = block_size / 4;
	idx = (uint32_t)(lblk - 12);
	if (idx >= per_indirect) return -1;
	iblk = ei->i_block[12];
	if (iblk == 0) return -1;
	ibuf = (char *)malloc(block_size);
	if (!ibuf) return -1;
	if (ext4_read_block(iblk, ibuf) < 0) { free(ibuf); return -1; }
	ents = (uint32_t *)ibuf;
	ents[idx] = 0;
	if (ext4_write_block(iblk, ibuf) < 0) { free(ibuf); return -1; }
	free(ibuf);
	return 0;
}

int ext4_htree_pick_leaf_from_root(struct inode *dir, uint32_t block_size,
				   char *root_buf, uint32_t h32, uint32_t *out_leaf_lblk)
{
	struct ext4_dx_root_info *info;
	struct ext4_dx_entry *entries;
	int cap, nent;
	uint32_t next_lblk;
	uint32_t next_phys;
	char *buf_node;
	struct ext4_dx_entry *nentries;
	int ncap, ncnt;

	if (ext4_htree_parse_root(root_buf, block_size, &info, &entries, &cap, &nent) < 0) return -1;
	if (info->indirect_levels == 0) {
		*out_leaf_lblk = ext4_htree_pick_leaf_lblock(entries, nent, h32);
		return 0;
	}
	if (info->indirect_levels != 1) return -1;
	next_lblk = ext4_htree_pick_leaf_lblock(entries, nent, h32);
	if (ext4_dir_get_blocknr(dir, next_lblk, &next_phys) < 0) return -1;
	buf_node = (char *)malloc(block_size);
	if (!buf_node) return -1;
	if (ext4_read_block(next_phys, buf_node) < 0) { free(buf_node); return -1; }
	if (ext4_htree_parse_node(buf_node, block_size, &nentries, &ncap, &ncnt) < 0) { free(buf_node); return -1; }
	*out_leaf_lblk = ext4_htree_pick_leaf_lblock(nentries, ncnt, h32);
	free(buf_node);
	(void)ncap;
	return 0;
}

int ext4_htree_try_insert_leaf(char *leaf, uint32_t block_size,
			       const struct qstr *name, unsigned long ino,
			       uint16_t need_rec, uint32_t *out_de_off)
{
	uint32_t off = 0;
	while (off + need_rec <= block_size) {
		struct ext4_dir_entry *de = (struct ext4_dir_entry *)(leaf + off);
		uint16_t d_rec = le16_to_cpu(de->rec_len);
		uint16_t d_nl = (uint16_t)de->name_len;
		uint32_t d_ino = le32_to_cpu(de->inode);
		if (d_rec == 0) return -1;
		if (d_ino == 0 || d_rec >= (uint16_t)(need_rec + (d_nl <= 0 ? 0 : EXT4_DIR_REC_LEN((int)d_nl)))) {
			uint16_t old_rec = d_rec;
			if (d_ino != 0 && old_rec > need_rec) {
				de->rec_len = (uint16_t)need_rec;
				de = (struct ext4_dir_entry *)((char *)de + need_rec);
				de->inode = (uint32_t)ino;
				de->rec_len = (uint16_t)(old_rec - need_rec);
			} else {
				de->inode = (uint32_t)ino;
				de->rec_len = old_rec;
			}
			de->name_len = (__u8)name->len;
			de->file_type = 0;
			if (name->name && name->len > 0) memcpy(de->name, name->name, (size_t)name->len);
			*out_de_off = (uint32_t)((char *)de - leaf);
			return 0;
		}
		off += d_rec;
	}
	return -1;
}

static int ext4_htree_leaf_collect(const struct inode *dir,
				   const char *leaf, uint32_t block_size,
				   struct ext4_htree_leaf_item *items, int *out_n)
{
	uint32_t off = 0;
	int n = 0;
	while (off < block_size) {
		struct ext4_dir_entry *de = (struct ext4_dir_entry *)(leaf + off);
		uint16_t rec = le16_to_cpu(de->rec_len);
		uint16_t nl = (uint16_t)de->name_len;
		uint32_t ino = le32_to_cpu(de->inode);
		struct qstr q;
		if (rec == 0) return -1;
		if (ino != 0 && nl > 0) {
			if (n >= EXT4_HTREE_SPLIT_MAX_NAMES) return -1;
			q.len = nl; q.name = (const unsigned char *)de->name; q.hash = 0;
			items[n].hash = ext4_htree_name_hash32(dir, &q);
			items[n].ino = ino;
			items[n].name_len = (__u8)nl;
			memcpy(items[n].name, de->name, (size_t)nl);
			n++;
		}
		off += rec;
	}
	*out_n = n;
	return 0;
}

static void ext4_htree_sort_items(struct ext4_htree_leaf_item *items, int n)
{
	int i, j;
	for (i = 0; i < n - 1; i++) {
		for (j = i + 1; j < n; j++) {
			if (items[i].hash > items[j].hash) {
				struct ext4_htree_leaf_item t = items[i];
				items[i] = items[j];
				items[j] = t;
			}
		}
	}
}

static void ext4_htree_pack_leaf(char *leaf, uint32_t block_size,
				 struct ext4_htree_leaf_item *items, int n)
{
	uint32_t pos = 0;
	int i;
	memset(leaf, 0, block_size);
	for (i = 0; i < n; i++) {
		uint16_t rl = EXT4_DIR_REC_LEN((int)items[i].name_len);
		struct ext4_dir_entry *de = (struct ext4_dir_entry *)(leaf + pos);
		de->inode = items[i].ino;
		de->rec_len = rl;
		de->name_len = items[i].name_len;
		de->file_type = 0;
		memcpy(de->name, items[i].name, (size_t)items[i].name_len);
		pos += rl;
	}
	if (pos < block_size) {
		struct ext4_dir_entry *tail = (struct ext4_dir_entry *)(leaf + pos);
		tail->inode = 0;
		tail->rec_len = (uint16_t)(block_size - pos);
		tail->name_len = 0;
		tail->file_type = 0;
	}
}

static void ext4_htree_refresh_dir_inode_size(struct inode *dir,
					      struct ext4_inode_info *ei,
					      uint32_t block_size)
{
	int j;
	unsigned cnt = 0;
	for (j = 0; j < 12; j++) if (ei->i_block[j] != 0) cnt = (unsigned)(j + 1);
	if (ei->i_block[12] != 0) {
		char *ibuf = (char *)malloc(block_size);
		if (ibuf && ext4_read_block(ei->i_block[12], ibuf) == 0) {
			uint32_t *ents = (uint32_t *)ibuf;
			uint32_t per = block_size / 4;
			uint32_t i;
			for (i = 0; i < per; i++) {
				if (le32_to_cpu(ents[i]) != 0) cnt = (unsigned)(12 + i + 1);
			}
		}
		if (ibuf) free(ibuf);
	}
	dir->i_size = (uint64_t)block_size * (uint64_t)cnt;
	dir->i_blocks = (uint64_t)(block_size / 512) * (uint64_t)cnt;
	if (ei->i_block[12] != 0) {
		dir->i_blocks += (uint64_t)(block_size / 512);
	}
}

int ext4_htree_split_leaf(struct inode *dir, struct super_block *sb,
			  struct ext4_inode_info *ei, uint32_t block_size,
			  char *root_buf, uint32_t root_phys,
			  uint32_t leaf_lblk, const struct qstr *name,
			  unsigned long ino, uint16_t need_rec)
{
	struct ext4_dx_root_info *info;
	struct ext4_dx_countlimit *root_cl;
	struct ext4_dx_entry *entries, *target_entries;
	struct ext4_dx_countlimit *target_cl;
	int cap, nent, idx;
	int target_cap, target_nent, target_phys;
	uint32_t leaf_phys;
	char *leaf_buf, *target_buf;
	struct ext4_htree_leaf_item *items = ext4_htree_split_items;
	int n, mid;
	uint32_t new_phys;
	int new_lblk;
	uint32_t H_mid;

	if (ext4_htree_parse_root(root_buf, block_size, &info, &entries, &cap, &nent) < 0) return -1;
	root_cl = (struct ext4_dx_countlimit *)(info + 1);
	target_entries = entries; target_cap = cap; target_nent = nent; target_phys = root_phys; target_buf = root_buf;
	target_cl = root_cl;

	if (info->indirect_levels == 1) {
		uint32_t h32 = ext4_htree_name_hash32(dir, name);
		uint32_t node_lblk = ext4_htree_pick_leaf_lblock(entries, nent, h32);
		uint32_t node_phys;
		if (ext4_dir_get_blocknr(dir, node_lblk, &node_phys) < 0) return -1;
		target_buf = (char *)malloc(block_size);
		if (!target_buf) return -1;
		if (ext4_read_block(node_phys, target_buf) < 0) { free(target_buf); return -1; }
		if (ext4_htree_parse_node(target_buf, block_size, &target_entries, &target_cap, &target_nent) < 0) { free(target_buf); return -1; }
		target_phys = node_phys;
		target_cl = &((struct ext4_dx_node *)target_buf)->countlimit;
	}

	idx = ext4_htree_find_dx_index_for_lblock(target_entries, target_nent, leaf_lblk);
	if (idx < 0) { if (target_buf != root_buf) free(target_buf); return -1; }
	if (ext4_dir_get_blocknr(dir, leaf_lblk, &leaf_phys) < 0) { if (target_buf != root_buf) free(target_buf); return -1; }

	leaf_buf = (char *)malloc(block_size);
	if (!leaf_buf) { if (target_buf != root_buf) free(target_buf); return -1; }
	if (ext4_read_block(leaf_phys, leaf_buf) < 0) { if (target_buf != root_buf) free(target_buf); free(leaf_buf); return -1; }
	if (ext4_htree_leaf_collect(dir, leaf_buf, block_size, items, &n) < 0) { if (target_buf != root_buf) free(target_buf); free(leaf_buf); return -1; }
	if (n >= EXT4_HTREE_SPLIT_MAX_NAMES) { if (target_buf != root_buf) free(target_buf); free(leaf_buf); return -1; }

	items[n].hash = ext4_htree_name_hash32(dir, name);
	items[n].ino = (uint32_t)ino;
	items[n].name_len = (__u8)name->len;
	if (name->name && name->len > 0) memcpy(items[n].name, name->name, (size_t)name->len);
	n++;
	ext4_htree_sort_items(items, n);
	mid = n / 2;
	if (mid < 1 || mid >= n) { if (target_buf != root_buf) free(target_buf); free(leaf_buf); return -1; }

	ext4_htree_pack_leaf(leaf_buf, block_size, items, mid);
	if (ext4_write_block(leaf_phys, leaf_buf) < 0) { if (target_buf != root_buf) free(target_buf); free(leaf_buf); return -1; }

	new_phys = ext4_new_block(sb);
	if (new_phys == 0) { if (target_buf != root_buf) free(target_buf); free(leaf_buf); return -1; }
	if (ext4_htree_alloc_lblock(sb, ei, new_phys, 1, &new_lblk) < 0) {
		ext4_free_block(sb, new_phys);
		if (target_buf != root_buf) free(target_buf);
		free(leaf_buf);
		return -1;
	}
	ext4_htree_pack_leaf(leaf_buf, block_size, items + mid, n - mid);
	if (ext4_write_block(new_phys, leaf_buf) < 0) {
		(void)ext4_htree_unmap_lblock(sb, ei, new_lblk);
		ext4_free_block(sb, new_phys);
		if (target_buf != root_buf) free(target_buf);
		free(leaf_buf);
		return -1;
	}
	free(leaf_buf);

	H_mid = items[mid].hash;
	if (target_nent + 1 > target_cap) {
		if (target_buf == root_buf && info->indirect_levels == 0) {
			char *node_buf = (char *)malloc(block_size);
			uint32_t node_phys;
			int node_lblk;
			struct ext4_dx_node *dn;
			struct ext4_dx_entry *ne;
			int i;

			if (!node_buf) return -1;
			node_phys = ext4_new_block(sb);
			if (node_phys == 0 || ext4_htree_alloc_lblock(sb, ei, node_phys, 1, &node_lblk) < 0) {
				if (node_phys) ext4_free_block(sb, node_phys);
				free(node_buf);
				return -1;
			}
			memset(node_buf, 0, block_size);
			dn = (struct ext4_dx_node *)node_buf;
			dn->fake_inum = 0;
			dn->fake_rec_len = (uint16_t)block_size;
			dn->fake_name_len = 0;
			dn->fake_file_type = 0;
			dn->info.reserved_zero = 0;
			dn->info.hash_version = info->hash_version;
			dn->info.info_length = (uint8_t)sizeof(struct ext4_dx_root_info);
			dn->info.indirect_levels = 0;
			dn->info.unused_flags = 0;
			dn->countlimit.limit = (uint16_t)((block_size - (uint32_t)sizeof(struct ext4_dx_node)) /
						       (uint32_t)sizeof(struct ext4_dx_entry));
			dn->countlimit.count = (uint16_t)(nent + 1);
			ne = (struct ext4_dx_entry *)&dn->countlimit;
			for (i = 0; i < nent; i++) ne[i] = entries[i];
			if (idx + 1 < nent) memmove(ne + idx + 2, ne + idx + 1, (size_t)(nent - idx - 1) * sizeof(*ne));
			/* entry[0].hash 与 count/limit 重叠，绝不能写。 */
			if (idx + 1 > 0) ne[idx + 1].hash = H_mid;
			ne[idx + 1].block = (uint32_t)new_lblk;
			if (ext4_write_block(node_phys, node_buf) < 0) { free(node_buf); return -1; }
			free(node_buf);
			root_cl->count = 1;
			entries[0].block = (uint32_t)node_lblk;
			info->indirect_levels = 1;
			if (ext4_write_block(root_phys, root_buf) < 0) return -1;
		} else {
			if (target_buf != root_buf) free(target_buf);
			return -1;
		}
	} else {
		if (idx + 1 < target_nent) memmove(target_entries + idx + 2, target_entries + idx + 1, (size_t)(target_nent - idx - 1) * sizeof(*target_entries));
		/* entry[0].hash 与 count/limit 重叠，绝不能写。 */
		if (idx + 1 > 0) target_entries[idx + 1].hash = H_mid;
		target_entries[idx + 1].block = (uint32_t)new_lblk;
		target_cl->count = (uint16_t)(target_nent + 1);
		if (ext4_write_block(target_phys, target_buf) < 0) { if (target_buf != root_buf) free(target_buf); return -1; }
	}
	if (target_buf != root_buf) free(target_buf);

	ext4_htree_refresh_dir_inode_size(dir, ei, block_size);
	if (dir->i_sb && dir->i_sb->s_op && dir->i_sb->s_op->write_inode)
		dir->i_sb->s_op->write_inode(dir, (struct writeback_control *)0);
	(void)need_rec;
	return 0;
}

