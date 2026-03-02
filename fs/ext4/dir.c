/*
 * Ext4 目录操作
 * 参考 Linux 内核 fs/ext4/namei.c、fs/ext4/dir.c
 * 实现目录项查找、添加、删除与遍历
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <fs/dentry.h>

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
#define memset simple_memset
#define memcpy simple_memcpy
#define memcmp simple_memcmp

/* 简化的内存分配（使用静态池） */
#define MAX_MALLOC 4096
#define MAX_MALLOC_BLOCKS 4
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

	/* 遍历直接块 */
	for (blk_idx = 0; blk_idx < 12; blk_idx++) {
		uint32_t blocknr = ei->i_block[blk_idx];
		if (blocknr == 0) {
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

	for (blk_idx = 0; blk_idx < 12; blk_idx++) {
		uint32_t blocknr = ei->i_block[blk_idx];
		if (blocknr == 0) {
			/* 目录块未分配（由调用方负责分配） */
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
			uint16_t d_name_len = le16_to_cpu(de->name_len);
			uint32_t d_ino = le32_to_cpu(de->inode);

			if (d_rec_len == 0) {
				free(buf);
				return -1;
			}
			/* 空闲槽（ino==0）或当前项可拆分出足够 rec_len */
			if (d_ino == 0 || d_rec_len >= (uint16_t)(rec_len + (d_name_len <= 0 ? 0 : EXT4_DIR_REC_LEN((int)d_name_len)))) {
				uint16_t old_rec = d_rec_len;
				if (d_ino != 0 && old_rec > rec_len) {
					/* 缩小当前项，在其后插入新项 */
					de->rec_len = (uint16_t)rec_len;
					de = (struct ext4_dir_entry *)((char *)de + rec_len);
					de->inode = (uint32_t)ino;
					de->rec_len = (uint16_t)(old_rec - rec_len);
					de->name_len = (uint16_t)name->len;
					if (name->name && name->len > 0)
						memcpy(de->name, name->name, (size_t)name->len);
				} else {
					/* 直接占用整个槽位 */
					de->inode = (uint32_t)ino;
					de->rec_len = old_rec;
					de->name_len = (uint16_t)name->len;
					if (name->name && name->len > 0)
						memcpy(de->name, name->name, (size_t)name->len);
				}
				ret = ext4_write_block(blocknr, buf);
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
	struct ext4_dir_entry *prev_de;
	uint32_t prev_blocknr;
	uint32_t prev_off;
	int ret;
	int first = 1;

	if (!ei || !name || name->len == 0) {
		return -1;
	}

	buf = (char *)malloc(block_size);
	if (!buf) {
		return -1;
	}

	prev_de = NULL;
	prev_blocknr = 0;
	prev_off = 0;

	for (blk_idx = 0; blk_idx < 12; blk_idx++) {
		uint32_t blocknr = ei->i_block[blk_idx];
		if (blocknr == 0) {
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
				/* 找到：与前一项合并或清空 inode */
				if (first) {
					/* 块内首项：仅将 inode 置 0 */
					de->inode = 0;
					de->name_len = 0;
					ret = ext4_write_block(blocknr, buf);
				} else {
					/* 将当前 rec_len 合并到前一项 */
					uint16_t prev_rec = le16_to_cpu(prev_de->rec_len);
					prev_de->rec_len = (uint16_t)(prev_rec + rec_len);
					ret = ext4_read_block(prev_blocknr, buf);
					if (ret >= 0) {
						prev_de = (struct ext4_dir_entry *)(buf + prev_off);
						prev_de->rec_len = (uint16_t)(prev_rec + rec_len);
						ret = ext4_write_block(prev_blocknr, buf);
					}
				}
				free(buf);
				return ret < 0 ? -1 : 0;
			}
			first = 0;
			prev_de = de;
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

	for (blk_idx = 0; blk_idx < 12; blk_idx++) {
		uint32_t blocknr = ei->i_block[blk_idx];
		if (blocknr == 0) {
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
