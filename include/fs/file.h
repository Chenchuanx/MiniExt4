/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * file 结构体定义
 * 按照 Linux 内核标准组织
 */
#ifndef _LINUX_FS_FILE_H
#define _LINUX_FS_FILE_H

#include <linux/types.h>
#include <fs/operations.h>
#include <fs/inode.h>
#include <fs/dentry.h>

/* 前向声明 */
struct vm_area_struct;

/**
 * file - 打开文件结构体
 * 
 * file 结构表示一个打开的文件，仅在内存中存在
 */
struct file {
	/* 关联的 inode 和 dentry */
	struct inode		*f_inode;	/* 关联的 inode */
	struct dentry		*f_path_dentry;	/* 路径 dentry */
	
	/* 文件操作函数 */
	const struct file_operations *f_op;	/* 文件操作函数表 */
	
	/* 文件位置 */
	loff_t			f_pos;		/* 文件偏移位置 */
	
	/* 标志 */
	unsigned int		f_flags;	/* 文件打开标志 */
	u32			f_mode;		/* 文件模式（读/写），等价于 fmode_t */
	
	/* 引用计数 */
	int			f_count;	/* 引用计数 */
	
	/* 私有数据 */
	void			*f_private;	/* 文件私有数据 */
};

/* 文件打开标志 */
#define O_RDONLY	00000000
#define O_WRONLY	00000001
#define O_RDWR		00000002
#define O_CREAT		00000100
#define O_EXCL		00000200
#define O_TRUNC		00001000
#define O_APPEND	00002000

#endif /* _LINUX_FS_FILE_H */

