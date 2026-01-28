/* 文件系统统计和属性结构，适用于 MiniExt4 项目 */
#ifndef _LINUX_FS_STAT_H
#define _LINUX_FS_STAT_H

#include <linux/types.h>
#include <fs/operations.h>

/**
 * kstatfs - 文件系统统计信息
 * 
 * 用于 statfs 系统调用，返回文件系统空间使用情况
 */
struct kstatfs {
	long		f_type;		/* 文件系统类型 */
	long		f_bsize;	/* 块大小 */
	u64		f_blocks;	/* 总块数 */
	u64		f_bfree;	/* 空闲块数 */
	u64		f_bavail;	/* 可用块数（考虑保留空间） */
	u64		f_files;	/* 总 inode 数 */
	u64		f_ffree;	/* 空闲 inode 数 */
	struct {
		int val[2];
	} f_fsid;		/* 文件系统 ID */
	long		f_namelen;	/* 最大文件名长度 */
	long		f_frsize;	/* 片段大小 */
	long		f_flags;	/* 挂载标志 */
	long		f_spare[4];	/* 预留字段 */
};

/**
 * kstat - 文件统计信息
 * 
 * 用于获取单个文件的属性信息
 */
struct kstat {
	u64		ino;		/* inode 编号 */
	dev_t		dev;		/* 设备号 */
	umode_t		mode;		/* 文件类型和权限 */
	unsigned int	nlink;		/* 硬链接数 */
	uid_t		uid;		/* 用户 ID */
	gid_t		gid;		/* 组 ID */
	dev_t		rdev;		/* 设备号（如果是设备文件） */
	loff_t		size;		/* 文件大小 */
	u64		blocks;		/* 分配的块数 */
	u64		mtime_ns;	/* 修改时间（纳秒） */
	u64		ctime_ns;	/* 状态改变时间（纳秒） */
	u64		atime_ns;	/* 访问时间（纳秒） */
	u32		blksize;	/* 块大小 */
	u32		attributes;	/* 文件属性 */
	u64		attributes_mask;	/* 属性掩码 */
};

/**
 * iattr - inode 属性
 * 
 * 用于设置 inode 属性
 */
struct iattr {
	unsigned int	ia_valid;	/* 有效属性掩码 */
	umode_t		ia_mode;	/* 文件类型和权限 */
	uid_t		ia_uid;		/* 用户 ID */
	gid_t		ia_gid;		/* 组 ID */
	loff_t		ia_size;	/* 文件大小 */
	time64_t	ia_atime;	/* 访问时间 */
	time64_t	ia_mtime;	/* 修改时间 */
	time64_t	ia_ctime;	/* 状态改变时间 */
};

/* iattr 有效属性掩码 */
#define ATTR_MODE	(1 << 0)	/* 设置模式 */
#define ATTR_UID	(1 << 1)	/* 设置用户 ID */
#define ATTR_GID	(1 << 2)	/* 设置组 ID */
#define ATTR_SIZE	(1 << 3)	/* 设置大小 */
#define ATTR_ATIME	(1 << 4)	/* 设置访问时间 */
#define ATTR_MTIME	(1 << 5)	/* 设置修改时间 */
#define ATTR_CTIME	(1 << 6)	/* 设置状态改变时间 */
#define ATTR_ATIME_SET	(1 << 7)	/* 设置访问时间（明确设置） */
#define ATTR_MTIME_SET	(1 << 8)	/* 设置修改时间（明确设置） */
#define ATTR_FORCE	(1 << 9)	/* 强制设置 */
#define ATTR_KILL_SUID	(1 << 10)	/* 清除 SUID */
#define ATTR_KILL_SGID	(1 << 11)	/* 清除 SGID */

/* 文件系统 ID 类型 */
typedef struct {
	int val[2];
} __kernel_fsid_t;

#endif /* _LINUX_FS_STAT_H */

