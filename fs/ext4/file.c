/*
 * Ext4 文件与目录的 file_operations
 * 参考 Linux 内核 fs/ext4/file.c、dir.c
 */

#include <linux/fs.h>
#include <fs/ext4/ext4.h>
#include <fs/dentry.h>

#ifndef NULL
#define NULL 0
#endif

/* 前向声明 */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern int ext4_write_block(uint32_t blocknr, const void *buf);
extern uint32_t ext4_get_block_size(void);

/* 简化的内存操作函数 */
static void *simple_memcpy(void *d, const void *s, size_t n)
{
	unsigned char *dd = (unsigned char *)d;
	const unsigned char *ss = (const unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) dd[i] = ss[i];
	return d;
}
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) p[i] = (unsigned char)c;
	return s;
}
#define memcpy simple_memcpy
#define memset simple_memset



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

/* 获取逻辑块号对应的数据块号 （传统直接/间接块映射）
 */
static int ext4_legacy_get_data_block(struct inode *inode, uint32_t lblock,
			       int create, uint32_t *out_block, int *is_new)
{
	struct super_block *sb = inode->i_sb;
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;
	uint32_t block_size = ext4_get_block_size();
	uint32_t ptrs_per_block = block_size / 4;
	uint32_t *ind_buf;
	uint32_t blk;
	int new_flag = 0;

	if (!ei || !out_block || block_size == 0) {
		return -1;
	}

	*out_block = 0;
	if (is_new) {
		*is_new = 0;
	}

	/* 直接块 */
	if (lblock < 12) {
		if (ei->i_block[lblock] == 0 && create) {
			blk = ext4_new_block(sb);
			if (blk == 0) {
				return -1;
			}
			ei->i_block[lblock] = blk;
			new_flag = 1;
		}
		*out_block = ei->i_block[lblock];
		if (is_new) {
			*is_new = new_flag;
		}
		return 0;
	}

	/* 单间接块：i_block[12] 指向间接块，间接块中存放数据块号数组 */
	lblock -= 12;
	if (lblock < ptrs_per_block) {
		ind_buf = (uint32_t *)malloc(block_size);
		if (!ind_buf) {
			return -1;
		}

		blk = ei->i_block[12];
		if (blk == 0) {
			if (!create) {
				free(ind_buf);
				return 0;
			}
			/* 分配间接块并清零 */
			blk = ext4_new_block(sb);
			if (blk == 0) {
				free(ind_buf);
				return -1;
			}
			ei->i_block[12] = blk;
			memset(ind_buf, 0, block_size);
			if (ext4_write_block(blk, ind_buf) < 0) {
				free(ind_buf);
				return -1;
			}
		} else {
			if (ext4_read_block(blk, ind_buf) < 0) {
				free(ind_buf);
				return -1;
			}
		}

		if (ind_buf[lblock] == 0 && create) {
			uint32_t data_blk = ext4_new_block(sb);
			if (data_blk == 0) {
				free(ind_buf);
				return -1;
			}
			ind_buf[lblock] = data_blk;
			if (ext4_write_block(blk, ind_buf) < 0) {
				free(ind_buf);
				return -1;
			}
			*out_block = data_blk;
			if (is_new) {
				*is_new = 1;
			}
		} else {
			*out_block = ind_buf[lblock];
			if (is_new) {
				*is_new = 0;
			}
		}

		free(ind_buf);
		return 0;
	}

	/* 双重间接块：i_block[13] 指向一级间接块，一级块中存放二级间接块号 */
	lblock -= ptrs_per_block;
	{
		uint32_t first_index, second_index;
		uint32_t first_blk;   /* 一级间接块号 */
		uint32_t second_blk;  /* 二级间接块号 */
		uint32_t *first_buf;

		/* 超出双重间接范围则直接返回 0（不支持三重间接） */
		if (lblock >= ptrs_per_block * ptrs_per_block) {
			return 0;
		}

		first_index = lblock / ptrs_per_block;
		second_index = lblock % ptrs_per_block;

		first_buf = (uint32_t *)malloc(block_size);
		if (!first_buf) {
			return -1;
		}

		first_blk = ei->i_block[13];
		if (first_blk == 0) {
			if (!create) {
				free(first_buf);
				return 0;
			}
			/* 分配一级间接块并清零 */
			first_blk = ext4_new_block(sb);
			if (first_blk == 0) {
				free(first_buf);
				return -1;
			}
			ei->i_block[13] = first_blk;
			memset(first_buf, 0, block_size);
			if (ext4_write_block(first_blk, first_buf) < 0) {
				free(first_buf);
				return -1;
			}
		} else {
			if (ext4_read_block(first_blk, first_buf) < 0) {
				free(first_buf);
				return -1;
			}
		}

		second_blk = first_buf[first_index];

		/* 读取或创建二级间接块 */
		ind_buf = (uint32_t *)malloc(block_size);
		if (!ind_buf) {
			free(first_buf);
			return -1;
		}

		if (second_blk == 0) {
			if (!create) {
				free(first_buf);
				free(ind_buf);
				return 0;
			}
			second_blk = ext4_new_block(sb);
			if (second_blk == 0) {
				free(first_buf);
				free(ind_buf);
				return -1;
			}
			first_buf[first_index] = second_blk;
			if (ext4_write_block(first_blk, first_buf) < 0) {
				free(first_buf);
				free(ind_buf);
				return -1;
			}
			memset(ind_buf, 0, block_size);
			if (ext4_write_block(second_blk, ind_buf) < 0) {
				free(first_buf);
				free(ind_buf);
				return -1;
			}
		} else {
			if (ext4_read_block(second_blk, ind_buf) < 0) {
				free(first_buf);
				free(ind_buf);
				return -1;
			}
		}

		/* 二级间接块中的实际数据块号 */
		if (ind_buf[second_index] == 0 && create) {
			uint32_t data_blk = ext4_new_block(sb);
			if (data_blk == 0) {
				free(first_buf);
				free(ind_buf);
				return -1;
			}
			ind_buf[second_index] = data_blk;
			if (ext4_write_block(second_blk, ind_buf) < 0) {
				free(first_buf);
				free(ind_buf);
				return -1;
			}
			*out_block = data_blk;
			if (is_new) {
				*is_new = 1;
			}
		} else {
			*out_block = ind_buf[second_index];
			if (is_new) {
				*is_new = 0;
			}
		}

		free(first_buf);
		free(ind_buf);
		return 0;
	}
}

/* 使用 extents 机制获取数据块号（fs/ext4/extents.c） */
extern int ext4_extents_get_block(struct inode *inode, uint32_t lblock,
				  int create, uint32_t *out_block, int *is_new);

/* 统一入口：根据 inode 是否开启 extents 决定具体实现 */
static int ext4_get_data_block(struct inode *inode, uint32_t lblock,
			       int create, uint32_t *out_block, int *is_new)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;

	/* 仅对普通文件启用 extents 机制 */
	if (S_ISREG(inode->i_mode)) {
		/* 如果是写入路径（create=1），则自动打开 extents 标志，确保后续
		 * 的数据块分配和映射都通过 extents 完成。
		 */
		if (create && ei && !(ei->i_flags & EXT4_INODE_FLAG_EXTENTS)) {
			ei->i_flags |= EXT4_INODE_FLAG_EXTENTS;
		}

		if (ei && (ei->i_flags & EXT4_INODE_FLAG_EXTENTS)) {
			/* 启用 extents：优先走 extents 版本 */
			int ret = ext4_extents_get_block(inode, lblock, create, out_block, is_new);
			if (ret == 0) {
				return 0;
			}
			/* extents 出错时再回退到传统实现，避免 I/O 直接失败 */
		}
	}

	/* 未启用 extents：使用传统直接/间接块映射 */
	return ext4_legacy_get_data_block(inode, lblock, create, out_block, is_new);
}

/* 普通文件操作 */

static int ext4_file_open(struct inode *inode, struct file *file)
{
	file->f_inode = inode;
	file->f_pos = 0;
	return 0;
}

static ssize_t ext4_file_read(struct file *file, char *buf, size_t count, loff_t *pos)
{
	struct inode *inode = file->f_inode;
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;
	uint32_t block_size = ext4_get_block_size();
	loff_t end;
	ssize_t read = 0;
	char *block_buf;

	if (!ei || !buf) return -1;
	end = *pos + (loff_t)count;
	if (end > inode->i_size) end = inode->i_size;
	if (*pos >= inode->i_size) return 0;
	count = (size_t)(end - *pos);
	if (count == 0) return 0;

	block_buf = (char *)malloc(block_size);
	if (!block_buf) return -1;

	while (count > 0) {
		uint32_t pos32 = (uint32_t)(*pos & 0xFFFFFFFFUL);
		uint32_t block_idx = pos32 / block_size;
		uint32_t off_in_block = pos32 % block_size;
		uint32_t blocknr;
		size_t to_copy;
		int ret;

		/* 通过直接块 + 间接块映射获取数据块号（不分配新块） */
		if (ext4_get_data_block(inode, block_idx, 0, &blocknr, 0) < 0) {
			free(block_buf);
			return -1;
		}
		if (blocknr == 0) break;
		ret = ext4_read_block(blocknr, block_buf);
		if (ret < 0) { free(block_buf); return -1; }
		to_copy = block_size - off_in_block;
		if (to_copy > count) to_copy = count;
		memcpy(buf, block_buf + off_in_block, to_copy);
		read += (ssize_t)to_copy;
		buf += to_copy;
		*pos += (loff_t)to_copy;
		count -= to_copy;
	}

	free(block_buf);
	return read;
}

static ssize_t ext4_file_write(struct file *file, const char *buf, size_t count, loff_t *pos)
{
	struct inode *inode = file->f_inode;
	struct super_block *sb = inode->i_sb;
	struct ext4_inode_info *ei = (struct ext4_inode_info *)inode->i_private;
	uint32_t block_size = ext4_get_block_size();
	loff_t end_pos = *pos + (loff_t)count;
	ssize_t written = 0;
	char *block_buf;

	if (!ei || !buf) return -1;
	block_buf = (char *)malloc(block_size);
	if (!block_buf) return -1;

	while (count > 0) {
		uint32_t pos32 = (uint32_t)(*pos & 0xFFFFFFFFUL);
		uint32_t block_idx = pos32 / block_size;
		uint32_t off_in_block = pos32 % block_size;
		uint32_t blocknr;
		size_t to_copy;
		int ret;
		int is_new = 0;

		/* 获取（必要时分配）数据块号，支持间接块 */
		if (ext4_get_data_block(inode, block_idx, 1, &blocknr, &is_new) < 0) {
			free(block_buf);
			return -1;
		}
		if (blocknr == 0) break;
		to_copy = block_size - off_in_block;
		if (to_copy > count) to_copy = count;
		/* 整块对齐写：直接从用户缓冲区落盘，避免 memset/memcpy 或读改写 */
		if (off_in_block == 0 && to_copy == block_size) {
			ret = ext4_write_block(blocknr, buf);
			if (ret < 0) { free(block_buf); return -1; }
		} else {
			if (is_new) {
				memset(block_buf, 0, block_size);
			} else {
				ret = ext4_read_block(blocknr, block_buf);
				if (ret < 0) { free(block_buf); return -1; }
			}
			memcpy(block_buf + off_in_block, buf, to_copy);
			ret = ext4_write_block(blocknr, block_buf);
			if (ret < 0) { free(block_buf); return -1; }
		}
		written += (ssize_t)to_copy;
		buf += to_copy;
		*pos += (loff_t)to_copy;
		count -= to_copy;

	}
	if (end_pos > inode->i_size) {
		uint32_t end32 = (uint32_t)(end_pos & 0xFFFFFFFFUL);
		inode->i_size = end_pos;
		inode->i_blocks = (unsigned long)((end32 + 511) / 512);
		/* 延迟写 inode，避免每次 write 都读改写 inode 表（顺序写大文件时开销极大） */
		inode->i_state |= I_DIRTY;
	}
	free(block_buf);
	return written;
}

static int ext4_file_release(struct inode *inode, struct file *file)
{
	(void)file;
	if (!inode || !inode->i_sb || !inode->i_sb->s_op ||
	    !inode->i_sb->s_op->write_inode)
		return 0;
	if (inode->i_state & I_DIRTY) {
		if (inode->i_sb->s_op->write_inode(inode, NULL) == 0)
			inode->i_state &= ~I_DIRTY;
	}
	return 0;
}

/* Ext4 普通文件操作表 */
const struct file_operations ext4_file_operations = {
	ext4_file_read,
	ext4_file_write,
	ext4_file_open,
	ext4_file_release,
	NULL,  /* readdir */
	NULL,  /* llseek */
	NULL,  /* mmap */
};

/* 目录 readdir 相关 */

struct ext4_readdir_ctx {
	void *dirent;
	filldir_t filldir;
	loff_t offset;
	loff_t start_pos;
};

static int ext4_readdir_fill(void *ctx, const char *name, int name_len,
			     unsigned long ino, unsigned int type)
{
	struct ext4_readdir_ctx *rctx = (struct ext4_readdir_ctx *)ctx;
	
	if (rctx->offset < rctx->start_pos) {
		rctx->offset++;
		return 0;
	}

	int r = rctx->filldir(rctx->dirent, name, name_len, rctx->offset, (u64)ino, type);
	if (r != 0) {
		return 1;
	}
	
	rctx->offset++;
	return 0;
}

static int ext4_dir_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *inode = file->f_inode;
	struct ext4_readdir_ctx ctx = {
		.dirent = dirent,
		.filldir = filldir,
		.offset = 0,
		.start_pos = file->f_pos,
	};
	int r = ext4_dir_foreach(inode, &ctx, ext4_readdir_fill);
	file->f_pos = ctx.offset;
	return r < 0 ? -1 : 0;
}

static int ext4_dir_open(struct inode *inode, struct file *file)
{
	file->f_inode = inode;
	file->f_pos = 0;
	return 0;
}

/* Ext4 目录操作表 */
const struct file_operations ext4_dir_operations = {
	NULL,                 /* read */
	NULL,                 /* write */
	ext4_dir_open,
	ext4_file_release,    /* release */
	ext4_dir_readdir,
	NULL,                 /* llseek */
	NULL,                 /* mmap */
};
