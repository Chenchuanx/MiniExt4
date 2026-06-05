/* 文件系统类型和块设备定义，适用于 MiniExt4 项目 */
#ifndef _LINUX_FS_TYPES_H
#define _LINUX_FS_TYPES_H

#include <linux/types.h>
#include <linux/list.h>
#include <fs/super.h>
#include <linux/blkdev.h>

/* 前向声明 */
struct dentry;
struct file_system_type;
struct super_block;

/* 文件系统标志 */
#define FS_REQUIRES_DEV		1	/* 需要块设备 */
#define FS_BINARY_MOUNTDATA	2	/* 二进制挂载数据 */
#define FS_HAS_SUBTYPE		4	/* 有子类型 */
#define FS_USERNS_MOUNT		8	/* 用户命名空间挂载 */

/**
 * file_system_type - 文件系统类型
 * 
 * 描述一种文件系统类型，用于注册和挂载文件系统
 */
struct file_system_type {
	const char		*name;		/* 文件系统名称（如 "ext4"） */
	int			fs_flags;	/* 文件系统标志 */
	
	/* 挂载函数 */
	struct dentry *(*mount)(struct file_system_type *fs_type,
				int flags, const char *dev_name, void *data);
	
	/* 卸载函数 */
	void (*kill_sb)(struct super_block *sb);
	
	/* 链表管理 */
	struct list_head	fs_supers;	/* 超级块链表 */
	
	/* 模块相关 */
	struct module		*owner;		/* 模块所有者（简化版可为 NULL） */
	
	/* 文件系统特性 */
	bool			fs_supers_initialized;	/* 是否已初始化 */

	/* 简化版文件系统类型链表指针（用于 register_filesystem） */
	struct file_system_type	*next;
};

/* 前向声明 */
struct module;

#endif /* _LINUX_FS_TYPES_H */

