/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * VFS 操作函数定义
 * 按照 Linux 内核标准组织
 */
#ifndef _LINUX_FS_OPERATIONS_H
#define _LINUX_FS_OPERATIONS_H

#include <linux/types.h>

/* 类型定义（必须在操作函数之前定义） */
typedef u16 umode_t;
typedef u32 uid_t;
typedef u32 gid_t;
typedef u32 fmode_t;

/* 前向声明 */
struct super_block;
struct inode;
struct dentry;
struct file;
struct kstatfs;
struct writeback_control;
struct kstat;
struct iattr;
struct path;
struct delayed_call;
struct qstr;
struct vm_area_struct;

/* 类型定义 */
typedef int (*filldir_t)(void *, const char *, int, loff_t, u64, unsigned);

/**
 * super_operations - 超级块操作函数
 */
struct super_operations {
	/* 分配并初始化 inode 结构体 */
	struct inode *(*alloc_inode)(struct super_block *sb);
	/* 销毁 inode 结构体，释放内存 */
	void (*destroy_inode)(struct inode *inode);
	/* 释放超级块资源，在卸载时调用 */
	void (*put_super)(struct super_block *sb);
	/* 同步文件系统，将缓存数据写入磁盘 */
	int (*sync_fs)(struct super_block *sb, int wait);
	/* 获取文件系统统计信息（总空间、可用空间等） */
	int (*statfs)(struct dentry *dentry, struct kstatfs *kstatfs);
	/* 重新挂载文件系统，修改挂载选项 */
	int (*remount_fs)(struct super_block *sb, int *flags, char *data);
	/* 卸载开始时的清理操作 */
	void (*umount_begin)(struct super_block *sb);
	/* 标记 inode 为脏（已修改），需要写回磁盘 */
	void (*dirty_inode)(struct inode *inode, int flags);
	/* 将 inode 数据写入磁盘 */
	int (*write_inode)(struct inode *inode, struct writeback_control *wbc);
	/* 从缓存中移除 inode，释放相关资源 */
	void (*evict_inode)(struct inode *inode);
};

/**
 * inode_operations - inode 操作函数
 */
struct inode_operations {
	/* 创建文件 */
	int (*create)(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
	/* 查找目录项 */
	struct dentry *(*lookup)(struct inode *dir, struct dentry *dentry, unsigned int flags);
	/* 创建链接 */
	int (*link)(struct dentry *old_dentry, struct inode *dir, struct dentry *new_dentry);
	/* 取消链接 */
	int (*unlink)(struct inode *dir, struct dentry *dentry);
	/* 创建符号链接 */
	int (*symlink)(struct inode *dir, struct dentry *dentry, const char *symname);
	/* 创建目录 */
	int (*mkdir)(struct inode *dir, struct dentry *dentry, umode_t mode);
	/* 删除目录 */
	int (*rmdir)(struct inode *dir, struct dentry *dentry);
	/* 创建特殊文件（设备文件等） */
	int (*mknod)(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev);
	/* 重命名 */
	int (*rename)(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry, unsigned int flags);
	/* 获取属性 */
	int (*getattr)(const struct path *path, struct kstat *stat, u32 request_mask, unsigned int query_flags);
	/* 设置属性 */
	int (*setattr)(struct dentry *dentry, struct iattr *attr);
	/* 读取符号链接目标 */
	const char *(*get_link)(struct dentry *dentry, struct inode *inode, struct delayed_call *done);
};

/**
 * dentry_operations - dentry 操作函数
 */
struct dentry_operations {
	/* 验证 dentry 是否有效 */
	int (*d_revalidate)(struct dentry *dentry, unsigned int flags);
	/* 比较两个 dentry */
	int (*d_compare)(const struct dentry *dentry, unsigned int len,
			 const char *str, const struct qstr *name);
	/* 删除 dentry */
	int (*d_delete)(const struct dentry *dentry);
	/* 释放 dentry */
	void (*d_release)(struct dentry *dentry);
	/* 释放 inode */
	void (*d_prune)(struct dentry *dentry);
	/* 初始化 dentry */
	void (*d_init)(struct dentry *dentry);
	/* 释放 dentry 名称 */
	void (*d_iput)(struct dentry *dentry, struct inode *inode);
	/* 生成哈希值 */
	int (*d_hash)(const struct dentry *dentry, struct qstr *qstr);
};

/**
 * file_operations - 文件操作函数
 */
struct file_operations {
	/* 读取文件 */
	ssize_t (*read)(struct file *file, char *buf, size_t count, loff_t *pos);
	/* 写入文件 */
	ssize_t (*write)(struct file *file, const char *buf, size_t count, loff_t *pos);
	/* 打开文件 */
	int (*open)(struct inode *inode, struct file *file);
	/* 释放文件 */
	int (*release)(struct inode *inode, struct file *file);
	/* 读取目录 */
	int (*readdir)(struct file *file, void *dirent, filldir_t filldir);
	/* 文件定位 */
	loff_t (*llseek)(struct file *file, loff_t offset, int whence);
	/* 内存映射 */
	int (*mmap)(struct file *file, struct vm_area_struct *vma);
};


#endif /* _LINUX_FS_OPERATIONS_H */

