/* dentry 结构体定义，适用于 MiniExt4 项目 */
#ifndef _LINUX_FS_DENTRY_H
#define _LINUX_FS_DENTRY_H

#include <linux/types.h>
#include <linux/list.h>
#include <fs/operations.h>
#include <fs/super.h>

/* 前向声明 */
struct inode;

/* dentry 标志 */
#define DCACHE_MANAGE_TRANSIT  0x00000001  /* 管理传输 */
#define DCACHE_MOUNTED         0x00000002  /* 挂载点 */
#define DCACHE_NEED_AUTOMOUNT  0x00000004  /* 需要自动挂载 */
#define DCACHE_MANAGED_DENTRY  0x00000008  /* 管理的 dentry */
#define DCACHE_LRU_LIST        0x00000010  /* LRU 列表 */
#define DCACHE_REFERENCED      0x00000020  /* 被引用 */
#define DCACHE_DISCONNECTED    0x00000040  /* 已断开连接 */
#define DCACHE_OP_SELECT_INODE 0x00000080  /* 选择 inode */
#define DCACHE_DENTRY_KILLED   0x00000100  /* dentry 已杀死 */
#define DCACHE_SHRINK_LIST     0x00000200  /* 收缩列表 */

/**
 * qstr - 快速字符串结构
 * 
 * 用于存储 dentry 名称的优化结构
 */
struct qstr {
	union {
		struct {
			u32 hash;
			u32 len;
		};
		u64 hash_len;
	};
	const unsigned char *name;
};

/**
 * dentry - 目录项结构体
 * 
 * dentry 是目录项缓存，用于快速查找文件路径
 */
struct dentry {
	/* 引用计数 */
	int			d_count;	/* 引用计数 */
	
	/* 标志和状态 */
	unsigned int		d_flags;	/* dentry 标志 */
	unsigned int		d_seq;		/* 序列号（用于 RCU） */
	
	/* 名称信息 */
	struct qstr		d_name;		/* 名称 */
	
	/* 父子关系 */
	struct dentry		*d_parent;	/* 父目录 dentry */
	struct list_head	d_child;	/* 父目录子项链表 */
	struct list_head	d_subdirs;	/* 子目录链表 */
	
	/* inode 关联 */
	struct inode		*d_inode;	/* 关联的 inode */
	
	/* 操作函数 */
	const struct dentry_operations *d_op;	/* dentry 操作函数 */
	
	/* 超级块 */
	struct super_block	*d_sb;		/* 所属超级块 */
	
	/* 私有数据 */
	void			*d_fsdata;	/* 文件系统私有数据 */
	
	/* 名称存储（小名称直接存储） */
	unsigned char		d_iname[32];	/* 短名称内联存储（DNAME_INLINE_LEN） */
};

/* 前向声明 */
struct qstr;

#endif /* _LINUX_FS_DENTRY_H */

