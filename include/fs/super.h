/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * super_block 结构体定义
 * 按照 Linux 内核标准组织
 */
#ifndef _LINUX_FS_SUPER_H
#define _LINUX_FS_SUPER_H

#include <linux/types.h>
#include <linux/list.h>

/* UUID 类型定义 */
#ifndef uuid_t
typedef struct {
	uint8_t b[16];
} uuid_t;
#endif

/* 超级块挂载标志 */
#define SB_RDONLY       (1UL << 0)  /* 只读挂载 */
#define SB_NOSUID       (1UL << 1)  /* 忽略 suid 和 sgid */
#define SB_NODEV        (1UL << 2)  /* 禁止访问设备文件 */
#define SB_NOEXEC       (1UL << 3)  /* 禁止执行程序 */
#define SB_SYNCHRONOUS  (1UL << 4)  /* 同步写入 */
#define SB_DIRSYNC      (1UL << 7)  /* 目录修改同步 */
#define SB_NOATIME      (1UL << 10) /* 不更新访问时间 */

/* 内部标志 */
#define SB_DEAD         (1UL << 21) /* 超级块已死亡 */
#define SB_BORN         (1UL << 29) /* 超级块已创建 */
#define SB_ACTIVE       (1UL << 30) /* 超级块活跃 */

#include <fs/operations.h>

/* 前向声明 */
struct file_system_type;
struct block_device;
struct dentry;

/**
 * super_block - 超级块结构体
 */
struct super_block {
	/* 链表管理 */
	struct list_head	s_list;
	
	/* 基础信息 */
	dev_t			s_dev;			/* 设备号 */
	unsigned char		s_blocksize_bits;	/* 块大小（位） */
	unsigned long		s_blocksize;		/* 块大小（字节） */
	loff_t			s_maxbytes;		/* 最大文件大小 */
	
	/* 文件系统类型和操作 */
	struct file_system_type		*s_type;	/* 文件系统类型 */
	const struct super_operations *s_op;		/* 操作函数表 */
	
	/* 标志和魔数 */
	unsigned long		s_flags;		/* 挂载标志 */
	unsigned long		s_magic;		/* 文件系统魔数 */
	
	/* 根目录 */
	struct dentry		*s_root;		/* 根目录 dentry */
	
	/* 引用计数（简化版，非原子操作） */
	int			s_count;		/* 引用计数 */
	int			s_active;		/* 活跃引用计数 */

	/* 卸载标志（简化版） */
	int			s_umount_flag;		/* 卸载标志 */
	
	/* 块设备 */
	struct block_device	*s_bdev;		/* 块设备指针 */

	/* 文件系统私有数据（Ext4 特定数据存储在这里） */
	void			*s_fs_info;		/* 文件系统私有信息 */
	
	/* 时间粒度 */
	u32			s_time_gran;		/* 时间粒度（纳秒） */

	/* 标识信息 */
	char			s_id[32];		/* 文件系统标识名 */
	uuid_t			s_uuid;			/* UUID */
	
	/* 预留字段，用于扩展 */
	unsigned long		_reserved[4];
};

/**
 * sb_rdonly - 检查超级块是否以只读模式挂载
 */
static inline bool sb_rdonly(const struct super_block *sb)
{
	return (sb->s_flags & SB_RDONLY) != 0;
}

#endif /* _LINUX_FS_SUPER_H */
