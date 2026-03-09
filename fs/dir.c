/*
 * VFS 目录操作封装
 * 将底层 file_operations 封装成简单的目录遍历接口，
 * 便于 syscalls 等上层代码复用，保持良好层次结构。
 */

#include <linux/fs.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 当前工作目录（简化：全局共享，尚未区分进程） */
static struct dentry *vfs_cwd = (struct dentry *)0;

/* 内部辅助：获取当前工作目录 dentry，若未初始化则默认为根目录 */
struct dentry *vfs_get_cwd_dentry(void)
{
	struct super_block *sb = vfs_get_root_sb();

	if (!sb || !sb->s_root) {
		return (struct dentry *)0;
	}

	if (!vfs_cwd) {
		vfs_cwd = sb->s_root;
	}

	return vfs_cwd;
}


/**
 * vfs_getcwd - 获取当前工作目录路径字符串
 * @buf:      输出缓冲区
 * @buf_len:  缓冲区长度
 *
 * 返回值：0 表示成功，负值表示错误。
 */
int vfs_getcwd(char *buf, int buf_len)
{
	struct super_block *sb;
	struct dentry *root;
	struct dentry *cwd;
	char *tmp;
	int pos;

	if (!buf || buf_len < 2) {
		return -1;
	}

	sb = vfs_get_root_sb();
	if (!sb || !sb->s_root) {
		return -2;
	}
	root = sb->s_root;
	cwd  = vfs_get_cwd_dentry();
	if (!cwd) {
		return -3;
	}

	/* 根目录：直接返回 "/" */
	if (cwd == root) {
		buf[0] = '/';
		buf[1] = '\0';
		return 0;
	}

	/* 从缓冲区末尾向前构造路径，最后再移动到开头 */
	pos = buf_len - 1;
	buf[pos] = '\0';

	tmp = buf;
	(void)tmp; /* 避免未使用变量告警（某些编译器） */

	while (cwd && cwd != root) {
		const unsigned char *name = cwd->d_name.name;
		int name_len = (int)cwd->d_name.len;
		int i;

		if (!name || name_len <= 0) {
			break;
		}

		/* 预留 '/' 和名称 */
		if (pos - (name_len + 1) <= 0) {
			return -4; /* 缓冲区不足 */
		}

		/* 先写入名称 */
		pos -= name_len;
		for (i = 0; i < name_len; i++) {
			buf[pos + i] = (char)name[i];
		}

		/* 再写入前导 '/' */
		pos--;
		buf[pos] = '/';

		cwd = cwd->d_parent;
	}

	/* 如果退回到了根目录，则当前构造的就是绝对路径 */
	if (pos <= 0) {
		return -5;
	}

	/* 将结果移动到缓冲区开头 */
	{
		int i = 0;
		while (buf[pos] != '\0') {
			buf[i++] = buf[pos++];
		}
		buf[i] = '\0';
	}

	return 0;
}

/**
 * vfs_mkdir - 创建目录
 *
 * 路径解析规则：
 *   - 以 '/' 开头：从根目录解析的绝对路径
 *   - 否则：       从当前工作目录解析的相对路径
 *
 * @path:  目录路径（最后一个分量为要创建的目录名）
 * @mode:  目录权限位（低 9 位生效）
 *
 * 返回值：0 表示成功，负值表示错误。
 */
int vfs_mkdir(const char *path, umode_t mode)
{
	struct super_block *sb;
	struct dentry *root;
	struct dentry *cwd;
	struct dentry *parent;
	const char *rel;
	const char *scan;
	const char *last_sep;
	const char *name_start;
	char name_buf[256];
	int name_len = 0;
	char parent_buf[256];
	int parent_len = 0;

	if (!path || path[0] == '\0') {
		return -1;
	}

	sb = vfs_get_root_sb();
	if (!sb || !sb->s_root || !sb->s_root->d_inode) {
		return -2;
	}
	root = sb->s_root;
	cwd  = vfs_get_cwd_dentry();
	if (!cwd) {
		return -3;
	}

	/* 去掉前导 '/'，得到相对路径部分 */
	rel = path;
	if (*rel == '/') {
		do {
			rel++;
		} while (*rel == '/');
	}
	if (*rel == '\0') {
		return -4;
	}

	/* 查找最后一个 '/'，将路径拆分为 parent/path 和 name */
	last_sep = 0;
	scan = rel;
	while (*scan != '\0') {
		if (*scan == '/') {
			last_sep = scan;
		}
		scan++;
	}

	if (!last_sep) {
		/* 没有 '/'：父目录就是起始目录（根或当前工作目录） */
		parent = (path[0] == '/') ? root : cwd;
		name_start = rel;
	} else {
		/* 有 '/'：前半部分是父路径，最后一个分量是新目录名 */
		parent_len = (int)(last_sep - rel);
		if (parent_len <= 0 || parent_len >= (int)sizeof(parent_buf)) {
			return -5;
		}
		for (name_len = 0; name_len < parent_len; name_len++) {
			parent_buf[name_len] = rel[name_len];
		}
		parent_buf[parent_len] = '\0';

		parent = (path[0] == '/') ? vfs_path_lookup(root, parent_buf)
					  : vfs_path_lookup(cwd, parent_buf);
		if (!parent || !parent->d_inode) {
			return -6;
		}
		if (!S_ISDIR(parent->d_inode->i_mode)) {
			return -7;
		}

		name_start = last_sep + 1;
		if (*name_start == '\0') {
			return -8;
		}
	}

	/* 拷贝名称分量，名称中不应再包含 '/' */
	name_len = 0;
	while (name_start[name_len] != '\0') {
		if (name_start[name_len] == '/') {
			/* 不允许名称中再出现 '/' */
			return -9;
		}
		if (name_len < 255) {
			name_buf[name_len] = name_start[name_len];
		}
		name_len++;
	}
	name_buf[name_len < 255 ? name_len : 255] = '\0';

	/* 目录 inode 必须支持 mkdir 操作 */
	if (!parent->d_inode || !parent->d_inode->i_op || !parent->d_inode->i_op->mkdir) {
		return -10;
	}

	/* 构造 qstr 和 dentry，调用底层文件系统的 mkdir */
	{
		struct qstr q;
		struct dentry *dentry;

		qstr_init(&q, name_buf, name_len);
		dentry = d_alloc(parent, &q);
		if (!dentry) {
			return -11;
		}

		if (parent->d_inode->i_op->mkdir(parent->d_inode, dentry, mode) != 0) {
			return -12;
		}
	}

	return 0;
}

/**
 * vfs_chdir - 改变当前工作目录
 *
 * @path:  目标目录路径。规则同 vfs_mkdir：
 *         - 以 '/' 开头：从根目录解析
 *         - 否则：       从当前工作目录解析
 *
 * 返回值：0 表示成功，负值表示错误。
 */
int vfs_chdir(const char *path)
{
	struct super_block *sb;
	struct dentry *root;
	struct dentry *cwd;
	struct dentry *target;

	if (!path || path[0] == '\0') {
		return -1;
	}

	sb = vfs_get_root_sb();
	if (!sb || !sb->s_root) {
		return -2;
	}
	root = sb->s_root;
	cwd  = vfs_get_cwd_dentry();
	if (!cwd) {
		return -3;
	}

	/* 解析路径 */
	if (path[0] == '/') {
		if (path[1] == '\0') {
			/* 特例：cd / 直接回到根目录 */
			target = root;
		} else {
			target = vfs_lookup_root(sb, path);
			if (!target) {
				return -4;
			}
		}
	} else {
		target = vfs_path_lookup(cwd, path);
		if (!target) {
			return -5;
		}
	}

	if (!target->d_inode) {
		return -6;
	}

	/* 必须是目录 */
	if (!S_ISDIR(target->d_inode->i_mode)) {
		return -7;
	}

	vfs_cwd = target;
	return 0;
}

