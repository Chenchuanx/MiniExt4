/* super_block 结构体定义, 适用于 MiniExt4 项目 */

#ifndef __SUPER_TYPE_H__    
#define __SUPER_TYPE_H__

#include <linux/types.h>
#include <linux/list.h>

/* UUID 类型定义（简化版，16字节） */
#ifndef uuid_t
typedef struct {
	uint8_t b[16];
} uuid_t;
#endif

struct super_block;
struct inode;
struct dentry;
struct file_system_type;
struct block_device;
struct kstatfs;
struct writeback_control;

/* 超级块挂载标志 */
#define SB_RDONLY       (1UL << 0)  /* 只读挂载 */
#define SB_NOSUID       (1UL << 1)  /* 忽略 suid 和 sgid */
#define SB_NODEV        (1UL << 2)  /* 禁止访问设备文件 */
#define SB_NOEXEC       (1UL << 3)  /* 禁止执行程序 */
#define SB_SYNCHRONOUS  (1UL << 4)  /* 同步写入 */
#define SB_DIRSYNC      (1UL << 7)  /* 目录修改同步 */
#define SB_NOATIME      (1UL << 10) /* 不更新访问时间 */

/* 内部标志（简化版） */
#define SB_DEAD         (1UL << 21) /* 超级块已死亡 */
#define SB_BORN         (1UL << 29) /* 超级块已创建 */
#define SB_ACTIVE       (1UL << 30) /* 超级块活跃 */

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

#endif /* __SUPER_TYPE_H__ */
