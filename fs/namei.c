/* 
 * VFS 路径解析（namei）
 * 参考 Linux 内核 fs/namei.c 的简化版
 * 
 * 实现从起始 dentry 出发，对路径字符串逐级执行 lookup，
 * 得到最终的 dentry（支持绝对路径、相对路径，以及 . / ..）。
 */

#include <linux/fs.h>
#include <fs/dentry.h>
#include <lib/printf.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 内部辅助：跳过路径中的连续 '/' */
static const char *skip_slashes(const char *path)
{
	if (!path) {
		return NULL;
	}
	while (*path == '/') {
		path++;
	}
	return path;
}

/**
 * next_component - 解析下一个路径分量
 * @path: 输入/输出，指向当前解析位置，返回时指向下一个分隔符（'/' 或 '\0'）
 * @name: 输出的名称缓冲区
 * @len:  输出的名称长度
 * 
 * 返回值：
 *   1  - 成功解析出一个分量
 *   0  - 没有更多分量（路径结束）
 *  -1  - 参数错误
 */
static int next_component(const char **path, char *name, int *len)
{
	const char *p;
	int n = 0;

	if (!path || !*path || !name || !len) {
		return -1;
	}

	p = skip_slashes(*path);
	if (!p || *p == '\0') {
		*path = p;
		*len = 0;
		return 0;
	}

	while (*p != '\0' && *p != '/') {
		if (n < 255) {
			name[n] = *p;
		}
		n++;
		p++;
	}
	name[n < 255 ? n : 255] = '\0';
	*len = n;
	*path = p;
	return 1;
}

/**
 * vfs_path_lookup - 从起始 dentry 出发解析路径字符串
 * @start: 起始 dentry（可为根目录 dentry 或当前目录 dentry）
 * @path:  路径字符串（支持绝对路径和相对路径）
 * 
 * 解析规则（简化版）：
 *   - 多个连续 '/' 视为一个分隔符
 *   - "."   保持在当前目录
 *   - ".."  返回父目录（若存在）
 *   - 其他  调用 i_op->lookup 逐级向下解析
 * 
 * 成功返回最终 dentry 指针，失败返回 NULL。
 */
struct dentry *vfs_path_lookup(struct dentry *start, const char *path)
{
	struct dentry *dentry;
	const char *p;
	char name_buf[256];
	int name_len;

	if (!start || !path) {
		return NULL;
	}

	/* 起点必须关联 inode */
	if (!start->d_inode) {
		return NULL;
	}

	dentry = start;
	p = path;

	/* 绝对路径：以 '/' 开头，从根开始；相对路径：从 start 开始 */
	p = skip_slashes(p);
	if (!p) {
		return NULL;
	}

	/* 特殊情况：路径为 "/" 或仅包含若干 '/' */
	if (*p == '\0') {
		return dentry;
	}

	while (1) {
		int ret = next_component(&p, name_buf, &name_len);
		struct inode *dir;

		if (ret < 0) {
			return NULL;
		}
		if (ret == 0) {
			/* 没有更多分量 */
			break;
		}

		/* 处理 "." */
		if (name_len == 1 && name_buf[0] == '.') {
			/* 保持在当前目录 */
			goto next;
		}

		/* 处理 ".." */
		if (name_len == 2 && name_buf[0] == '.' && name_buf[1] == '.') {
			if (dentry->d_parent) {
				dentry = dentry->d_parent;
			}
			goto next;
		}

		dir = dentry->d_inode;
		if (!dir || !dir->i_op || !dir->i_op->lookup) {
			return NULL;
		}

		/* 构造 qstr，并优先使用 dcache */
		{
			struct qstr q;
			struct dentry *child;

			qstr_init(&q, name_buf, name_len);

			/* 先查 dcache */
			child = d_lookup(dentry, &q);
			if (!child) {
				/* 未命中缓存：分配新 dentry，并让文件系统 lookup 填充 */
				child = d_alloc(dentry, &q);
				if (!child) {
					return NULL;
				}
				/* 文件系统的 lookup 负责设置 child->d_inode */
				child = dir->i_op->lookup(dir, child, 0);
			}

			if (!child || !child->d_inode) {
				/* 未找到目标 */
				return NULL;
			}

			dentry = child;
		}

next:
		/* 跳过本分量后的所有 '/'，继续解析 */
		p = skip_slashes(p);
		if (!p || *p == '\0') {
			break;
		}
	}

	return dentry;
}

/**
 * vfs_lookup_root - 以超级块根 dentry 为起点解析路径
 * @sb:   超级块
 * @path: 路径字符串
 */
struct dentry *vfs_lookup_root(struct super_block *sb, const char *path)
{
	if (!sb || !sb->s_root) {
		return NULL;
	}
	return vfs_path_lookup(sb->s_root, path);
}

