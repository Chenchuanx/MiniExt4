/*
 * Ext4 目录操作
 * 参考 Linux 内核 fs/ext4/namei.c、fs/ext4/dir.c
 * 实现目录项查找、添加、删除与遍历
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <fs/bptree.h>

/* 需要完整类型定义以在本文件中声明静态 bptree_node 池 */
struct bptree_node;
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

/* 简化的内存操作函数 */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) p[i] = (unsigned char)c;
	return s;
}
static void *simple_memcpy(void *d, const void *s, size_t n)
{
	unsigned char *dd = (unsigned char *)d;
	const unsigned char *ss = (const unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) dd[i] = ss[i];
	return d;
}
static int simple_memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *aa = (const unsigned char *)a;
	const unsigned char *bb = (const unsigned char *)b;
	size_t i;
	for (i = 0; i < n; i++) if (aa[i] != bb[i]) return aa[i] - bb[i];
	return 0;
}
static void *simple_memmove(void *d, const void *s, size_t n)
{
	unsigned char *dd = (unsigned char *)d;
	const unsigned char *ss = (const unsigned char *)s;
	size_t i;

	if (!d || !s || n == 0)
		return d;
	if (dd < ss) {
		for (i = 0; i < n; i++)
			dd[i] = ss[i];
	} else {
		for (i = n; i > 0; i--)
			dd[i - 1] = ss[i - 1];
	}
	return d;
}
#define memset simple_memset
#define memcpy simple_memcpy
#define memcmp simple_memcmp
#define memmove simple_memmove

/* 简化的内存分配（使用静态池） */
#define MAX_MALLOC 4096
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

/* === 目录 B+Tree 索引（仅内存） ======================================= */

struct ext4_dir_index_entry {
	u64 key;            /* 目前直接使用简单 hash 作为 key，后续可替换为 HTree hash */
	uint32_t blocknr;   /* 目录项所在的物理块号 */
	uint32_t offset;    /* 在块内的字节偏移 */
};

/* 目录项哈希回调，便于后续替换为真正的 ext4 HTree hash 或其他策略 */
typedef u64 (*ext4_dir_hash_fn)(const struct qstr *name, void *ctx);

struct ext4_dir_index {
	struct bptree_root root;                         /* B+Tree 根 */
	struct ext4_dir_index_entry entries[64];         /* 简化：最多缓存 64 个索引项 */
	int used;
	ext4_dir_hash_fn hash_fn;                        /* 名字 -> key 的哈希函数 */
	void *hash_ctx;                                  /* 传给哈希函数的上下文 */
};

/* 前向声明：目录索引构建与块号获取函数 */
static void ext4_dir_index_build(struct inode *dir, struct ext4_dir_index *idx);
static int ext4_dir_get_blocknr(struct inode *dir, uint32_t lblock,
				uint32_t *out_blocknr);

static struct bptree_node *ext4_dir_bpt_alloc_node(void)
{
	/* 这里直接使用简单的静态分配池，以避免依赖通用 malloc。
	 * 若目录很多/更复杂时，可以改成按 ext4_dir_index 成员池来分配。*/
	static struct bptree_node node_pool[64];
	static int node_used[64];
	int i;

	for (i = 0; i < 64; i++) {
		if (!node_used[i]) {
			node_used[i] = 1;
			return &node_pool[i];
		}
	}
	return (struct bptree_node *)0;
}

static void ext4_dir_bpt_free_node(struct bptree_node *node)
{
	/* 简化：不真正回收，当前实现只做插入，不做删除，故可以忽略回收 */
	(void)node;
}

/* 默认名字 hash 实现：FNV-1a 64-bit。
 * 作为可替换策略的一个默认实现，真正的 ext4 HTree 可以通过自定义 hash_fn 注入。 */
static u64 ext4_dir_default_hash(const struct qstr *name, void *ctx)
{
	u64 h = 0xcbf29ce484222325ULL; /* FNV-1a 64-bit offset basis */
	size_t i;

	(void)ctx;
	if (!name || !name->name)
		return 0;
	for (i = 0; i < (size_t)name->len; i++) {
		h ^= (u64)name->name[i];
		h *= 0x100000001b3ULL; /* FNV prime */
	}
	return h;
}

static struct ext4_dir_index *ext4_dir_get_index(struct inode *dir)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	struct ext4_dir_index *idx;

	if (!ei)
		return (struct ext4_dir_index *)0;

	if (ei->dir_index)
		return (struct ext4_dir_index *)ei->dir_index;

	/* 第一次访问该目录：懒初始化一个索引结构 */
	idx = (struct ext4_dir_index *)malloc(sizeof(struct ext4_dir_index));
	if (!idx)
		return (struct ext4_dir_index *)0;

	memset(idx, 0, sizeof(*idx));
	bptree_init(&idx->root, ext4_dir_bpt_alloc_node, ext4_dir_bpt_free_node);

	/* 初始化默认 hash 策略，后续如果需要可以在其他地方替换 hash_fn/hash_ctx */
	idx->hash_fn = ext4_dir_default_hash;
	idx->hash_ctx = NULL;

	/* 基于当前目录的所有 on-disk 目录项，构建一次完整的哈希索引树。
	 * 之后 find/add/remove 可以都走 B+Tree，接近真正的 HTree 行为。 */
	ext4_dir_index_build(dir, idx);

	ei->dir_index = idx;
	return idx;
}

/* 目录项变更后丢弃内存中的哈希索引，避免 remove 后 B+Tree 仍指向旧偏移 */
static void ext4_dir_index_invalidate(struct inode *dir)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)dir->i_private;
	struct ext4_dir_index *idx;

	if (!ei || !ei->dir_index)
		return;
	idx = (struct ext4_dir_index *)ei->dir_index;
	free(idx);
	ei->dir_index = NULL;
}

/* 在给定索引结构中添加一条项（内部使用） */
static void ext4_dir_index_add_idx(struct ext4_dir_index *idx,
				   const struct qstr *name,
				   uint32_t blocknr, uint32_t off)
{
	struct ext4_dir_index_entry *e;
	u64 key;

	if (!idx || idx->used >= (int)(sizeof(idx->entries) / sizeof(idx->entries[0])))
		return;

	if (!idx->hash_fn)
		return;

	key = idx->hash_fn(name, idx->hash_ctx);

	e = &idx->entries[idx->used++];
	e->key = key;
	e->blocknr = blocknr;
	e->offset = off;

	(void)bptree_insert(&idx->root, key, e);
}

/* 封装一层：从 inode 获取/初始化索引后再添加 */
static void ext4_dir_index_add(struct inode *dir, const struct qstr *name,
			       uint32_t blocknr, uint32_t off)
{
	struct ext4_dir_index *idx = ext4_dir_get_index(dir);
	if (!idx)
		return;
	ext4_dir_index_add_idx(idx, name, blocknr, off);
}

/* 首次为某个目录创建索引时，对整个目录做一次全量扫描并建立 B+Tree
 * 这样 ext4_find_entry 随后就可以真正作为 “HTree” 式的哈希索引来用。 */
static void ext4_dir_index_build(struct inode *dir, struct ext4_dir_index *idx)
{
	uint32_t block_size = ext4_get_block_size();
	char *buf;
	uint32_t blk_idx;
	uint32_t off;
	int ret;

	if (!dir || !idx)
		return;

	buf = (char *)malloc(block_size);
	if (!buf)
		return;

	for (blk_idx = 0; ; blk_idx++) {
		uint32_t blocknr;

		if (ext4_dir_get_blocknr(dir, blk_idx, &blocknr) < 0)
			break;

		ret = ext4_read_block(blocknr, buf);
		if (ret < 0)
			break;

		off = 0;
		while (off < block_size) {
			struct ext4_dir_entry *de = (struct ext4_dir_entry *)(buf + off);
			uint16_t rec_len = le16_to_cpu(de->rec_len);
			uint16_t name_len = (uint16_t)de->name_len;
			uint32_t ino = le32_to_cpu(de->inode);

			if (rec_len == 0)
				goto out;

			if (ino != 0 && name_len > 0) {
				struct qstr q;
				q.len = name_len;
				q.name = (const unsigned char *)de->name;
				/* q.hash 未使用，置 0 即可 */
				q.hash = 0;

				ext4_dir_index_add_idx(idx, &q, blocknr, off);
			}

			off += rec_len;
		}
	}

out:
	free(buf);
}

static int ext4_dir_index_lookup(struct inode *dir, const struct qstr *name,
				 uint32_t *blocknr, uint32_t *off)
{
	struct ext4_dir_index *idx = ext4_dir_get_index(dir);
	u64 key;
	struct ext4_dir_index_entry *e;

	if (!idx || !idx->hash_fn)
		return -1;

	key = idx->hash_fn(name, idx->hash_ctx);
	e = (struct ext4_dir_index_entry *)bptree_search(&idx->root, key);
	if (!e)
		return -1;

	*blocknr = e->blocknr;
	*off = e->offset;
	return 0;
}

/* 统一获取目录的数据块号：
 * - 对于未开启 extents 的 inode，直接使用 i_block[0..11] 作为块号；
 * - 对于开启 EXTENTS 标志的目录，使用 extents 机制解析逻辑块号。
 */
static int ext4_dir_get_blocknr(struct inode *dir, uint32_t lblock,
				uint32_t *out_blocknr)
{
	struct ext4_inode_info *ei;

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

	/* 未开启 EXTENTS：沿用传统直接块数组 i_block[0..11] */
	if (lblock >= 12) {
		return -1;
	}
	if (ei->i_block[lblock] == 0) {
		return -1;
	}
	*out_blocknr = ei->i_block[lblock];
	return 0;
}

/* ========== HTree 单层索引抽象（多叶子；后续多层可在同接口下接 dx_node）==========
 *
 * 约定（与 ext4_mkdir 写入的根块一致）：
 * - 逻辑块 0：根块，含 . / ..、伪目录项、ext4_dx_root_info、ext4_dx_entry[]
 * - 逻辑块 1..N：叶子块，仅含普通目录项
 * - dx_entry[i].hash 为该叶子内名字哈希的上界（含）；最后一条为 0xffffffff
 * - dx_entry[i].block 为逻辑块号（与 i_block[] 下标一致）
 */

static uint32_t ext4_htree_name_hash32(const struct qstr *name)
{
	u64 h = ext4_dir_default_hash(name, NULL);
	return (uint32_t)(h ^ (h >> 32));
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
	struct ext4_dir_entry *fake = (struct ext4_dir_entry *)(root + r1 + r2);
	uint32_t fake_off = (uint32_t)(r1 + r2);
	uint16_t fake_len = le16_to_cpu(fake->rec_len);
	char *fake_end = (char *)root + fake_off + fake_len;
	struct ext4_dx_root_info *info;
	struct ext4_dx_entry *entries;
	int cap, i;

	if (fake_off + fake_len > block_size || fake_len < (int)sizeof(struct ext4_dir_entry))
		return -1;

	info = (struct ext4_dx_root_info *)((char *)fake + sizeof(struct ext4_dir_entry));
	entries = (struct ext4_dx_entry *)(info + 1);
	if ((char *)(entries + 1) > fake_end)
		return -1;

	cap = (int)(fake_end - (char *)entries) / (int)sizeof(struct ext4_dx_entry);
	if (cap < 1)
		return -1;

	for (i = 0; i < cap; i++) {
		if (le32_to_cpu(entries[i].hash) == 0xffffffffU) {
			*out_info = info;
			*out_entries = entries;
			*out_cap = cap;
			*out_count = i + 1;
			return 0;
		}
	}
	return -1;
}

static uint32_t ext4_htree_pick_leaf_lblock(struct ext4_dx_entry *entries, int n,
					    uint32_t h32)
{
	int i;

	for (i = 0; i < n; i++) {
		if (h32 <= le32_to_cpu(entries[i].hash))
			return le32_to_cpu(entries[i].block);
	}
	return le32_to_cpu(entries[n - 1].block);
}

static int ext4_htree_find_dx_index_for_lblock(struct ext4_dx_entry *entries, int n,
					       uint32_t lblk)
{
	int i;

	for (i = 0; i < n; i++) {
		if (le32_to_cpu(entries[i].block) == lblk)
			return i;
	}
	return -1;
}

/* 在叶子缓冲区内尝试插入一条目录项；成功返回 0，并写出 de 在块内偏移 */
static int ext4_htree_try_insert_leaf(char *leaf, uint32_t block_size,
				      const struct qstr *name, unsigned long ino,
				      uint16_t need_rec, uint32_t *out_de_off)
{
	uint32_t off = 0;

	while (off + need_rec <= block_size) {
		struct ext4_dir_entry *de = (struct ext4_dir_entry *)(leaf + off);
		uint16_t d_rec = le16_to_cpu(de->rec_len);
		uint16_t d_nl = (uint16_t)de->name_len;
		uint32_t d_ino = le32_to_cpu(de->inode);

		if (d_rec == 0)
			return -1;
		if (d_ino == 0 || d_rec >= (uint16_t)(need_rec + (d_nl <= 0 ? 0 : EXT4_DIR_REC_LEN((int)d_nl)))) {
			uint16_t old_rec = d_rec;
			if (d_ino != 0 && old_rec > need_rec) {
				de->rec_len = (uint16_t)need_rec;
				de = (struct ext4_dir_entry *)((char *)de + need_rec);
				de->inode = (uint32_t)ino;
				de->rec_len = (uint16_t)(old_rec - need_rec);
				de->name_len = (__u8)name->len;
				de->file_type = 0;
				if (name->name && name->len > 0)
					memcpy(de->name, name->name, (size_t)name->len);
			} else {
				de->inode = (uint32_t)ino;
				de->rec_len = old_rec;
				de->name_len = (__u8)name->len;
				de->file_type = 0;
				if (name->name && name->len > 0)
					memcpy(de->name, name->name, (size_t)name->len);
			}
			*out_de_off = (uint32_t)((char *)de - leaf);
			return 0;
		}
		off += d_rec;
	}
	return -1;
}

#define EXT4_HTREE_SPLIT_MAX_NAMES 256

struct ext4_htree_leaf_item {
	uint32_t hash;
	uint32_t ino;
	uint8_t name_len;
	char name[255];
};

static int ext4_htree_leaf_collect(const char *leaf, uint32_t block_size,
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

		if (rec == 0)
			return -1;
		if (ino != 0 && nl > 0) {
			if (n >= EXT4_HTREE_SPLIT_MAX_NAMES)
				return -1;
			q.len = nl;
			q.name = (const unsigned char *)de->name;
			q.hash = 0;
			items[n].hash = ext4_htree_name_hash32(&q);
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

/* 将 items[0..n) 顺序打包到一个叶子块（末尾一条大空闲记录） */
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

	for (j = 0; j < 12; j++) {
		if (ei->i_block[j] != 0)
			cnt = (unsigned)(j + 1);
	}
	dir->i_size = (uint64_t)block_size * (uint64_t)cnt;
	dir->i_blocks = (uint64_t)(block_size / 512) * (uint64_t)cnt;
}

/* 叶满：分裂当前叶，更新根块 dx 表与 inode 块指针；成功返回 0 */
static int ext4_htree_split_leaf(struct inode *dir, struct super_block *sb,
				 struct ext4_inode_info *ei, uint32_t block_size,
				 char *root_buf, uint32_t root_phys,
				 uint32_t leaf_lblk, const struct qstr *name,
				 unsigned long ino, uint16_t need_rec)
{
	struct ext4_dx_root_info *info;
	struct ext4_dx_entry *entries;
	int cap, nent, idx;
	uint32_t leaf_phys;
	char *leaf_buf;
	struct ext4_htree_leaf_item items[EXT4_HTREE_SPLIT_MAX_NAMES];
	int n, mid, i;
	uint32_t new_phys;
	int new_lblk;
	uint32_t H_old, H_mid;

	if (ext4_htree_parse_root(root_buf, block_size, &info, &entries, &cap, &nent) < 0)
		return -1;
	idx = ext4_htree_find_dx_index_for_lblock(entries, nent, leaf_lblk);
	if (idx < 0)
		return -1;
	if (ext4_dir_get_blocknr(dir, leaf_lblk, &leaf_phys) < 0)
		return -1;

	leaf_buf = (char *)malloc(block_size);
	if (!leaf_buf)
		return -1;
	if (ext4_read_block(leaf_phys, leaf_buf) < 0) {
		free(leaf_buf);
		return -1;
	}
	if (ext4_htree_leaf_collect(leaf_buf, block_size, items, &n) < 0) {
		free(leaf_buf);
		return -1;
	}
	if (n >= EXT4_HTREE_SPLIT_MAX_NAMES) {
		free(leaf_buf);
		return -1;
	}
	items[n].hash = ext4_htree_name_hash32(name);
	items[n].ino = (uint32_t)ino;
	items[n].name_len = (__u8)name->len;
	if (name->name && name->len > 0)
		memcpy(items[n].name, name->name, (size_t)name->len);
	n++;

	ext4_htree_sort_items(items, n);

	mid = n / 2;
	if (mid < 1 || mid >= n) {
		free(leaf_buf);
		return -1;
	}

	ext4_htree_pack_leaf(leaf_buf, block_size, items, mid);
	if (ext4_write_block(leaf_phys, leaf_buf) < 0) {
		free(leaf_buf);
		return -1;
	}

	new_phys = ext4_new_block(sb);
	if (new_phys == 0) {
		free(leaf_buf);
		return -1;
	}
	for (new_lblk = 1; new_lblk < 12; new_lblk++) {
		if (ei->i_block[new_lblk] == 0) {
			ei->i_block[new_lblk] = new_phys;
			break;
		}
	}
	if (new_lblk >= 12) {
		ext4_free_block(sb, new_phys);
		free(leaf_buf);
		return -1;
	}

	ext4_htree_pack_leaf(leaf_buf, block_size, items + mid, n - mid);
	if (ext4_write_block(new_phys, leaf_buf) < 0) {
		ei->i_block[new_lblk] = 0;
		ext4_free_block(sb, new_phys);
		free(leaf_buf);
		return -1;
	}
	free(leaf_buf);

	H_mid = items[mid - 1].hash;
	H_old = le32_to_cpu(entries[idx].hash);
	if (nent + 1 > cap)
		return -1;

	if (idx + 1 < nent)
		memmove(entries + idx + 2, entries + idx + 1,
			(size_t)(nent - idx - 1) * sizeof(*entries));

	/* 小端磁盘布局：直接写 __le32 字段 */
	entries[idx].hash = H_mid;
	entries[idx + 1].hash = H_old;
	entries[idx + 1].block = (uint32_t)new_lblk;

	if (ext4_write_block(root_phys, root_buf) < 0)
		return -1;

	ext4_htree_refresh_dir_inode_size(dir, ei, block_size);
	if (dir->i_sb && dir->i_sb->s_op && dir->i_sb->s_op->write_inode)
		dir->i_sb->s_op->write_inode(dir, NULL);

	(void)need_rec;
	(void)info;
	return 0;
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
		struct ext4_dx_root_info *info;
		struct ext4_dx_entry *entries;
		int cap, nent;
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
		    ext4_read_block(root_phys, buf_root) < 0 ||
		    ext4_htree_parse_root(buf_root, block_size, &info, &entries, &cap, &nent) < 0) {
			free(buf_root);
			free(buf_leaf);
			free(buf);
			return -1;
		}
		(void)info;

		h32 = ext4_htree_name_hash32(name);
		leaf_lblk = ext4_htree_pick_leaf_lblock(entries, nent, h32);
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
	for (blk_idx = 0; blk_idx < 12; blk_idx++) {
		uint32_t blocknr = ei->i_block[blk_idx];
		if (blocknr == 0) {
			free(buf);
			return -1;
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

			if (d_rec_len == 0) {
				free(buf);
				return -1;
			}
			if (d_ino == 0 || d_rec_len >= (uint16_t)(rec_len + (d_name_len <= 0 ? 0 : EXT4_DIR_REC_LEN((int)d_name_len)))) {
				uint16_t old_rec = d_rec_len;
				if (d_ino != 0 && old_rec > rec_len) {
					de->rec_len = (uint16_t)rec_len;
					de = (struct ext4_dir_entry *)((char *)de + rec_len);
					de->inode = (uint32_t)ino;
					de->rec_len = (uint16_t)(old_rec - rec_len);
					de->name_len = (__u8)name->len;
					de->file_type = 0;
					if (name->name && name->len > 0)
						memcpy(de->name, name->name, (size_t)name->len);
				} else {
					de->inode = (uint32_t)ino;
					de->rec_len = old_rec;
					de->name_len = (__u8)name->len;
					de->file_type = 0;
					if (name->name && name->len > 0)
						memcpy(de->name, name->name, (size_t)name->len);
				}
				ret = ext4_write_block(blocknr, buf);
				if (ret >= 0) {
					ext4_dir_index_add(dir, name, blocknr,
							   (uint32_t)((char *)de - buf));
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
