/* inode 结构体定义，适用于 MiniExt4 项目 */
#ifndef _LINUX_FS_INODE_H
#define _LINUX_FS_INODE_H

#include <linux/types.h>
#include <linux/list.h>
#include <fs/operations.h>
#include <fs/super.h>

/* 前向声明 */
struct file;
struct address_space;
struct posix_acl;

/* inode 模式位 */
#define S_IFMT   0170000  /* 文件类型掩码 */
#define S_IFSOCK 0140000  /* socket */
#define S_IFLNK  0120000  /* 符号链接 */
#define S_IFREG  0100000  /* 普通文件 */
#define S_IFBLK  0060000  /* 块设备 */
#define S_IFDIR  0040000  /* 目录 */
#define S_IFCHR  0020000  /* 字符设备 */
#define S_IFIFO  0010000  /* FIFO */

/* 权限位 */
#define S_ISUID  0004000  /* set user id on execution */
#define S_ISGID  0002000  /* set group id on execution */
#define S_ISVTX  0001000  /* sticky bit */

/* 所有者权限 */
#define S_IRWXU  0000700  /* rwx------ */
#define S_IRUSR  0000400  /* r-------- */
#define S_IWUSR  0000200  /* -w------- */
#define S_IXUSR  0000100  /* --x------ */

/* 组权限 */
#define S_IRWXG  0000070  /* ---rwx--- */
#define S_IRGRP  0000040  /* ---r----- */
#define S_IWGRP  0000020  /* ----w---- */
#define S_IXGRP  0000010  /* -----x--- */

/* 其他用户权限 */
#define S_IRWXO  0000007  /* ------rwx */
#define S_IROTH  0000004  /* ------r-- */
#define S_IWOTH  0000002  /* -------w- */
#define S_IXOTH  0000001  /* --------x */

/* 文件类型检查宏 */
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* inode 标志 */
#define I_NEW         0x00000080  /* inode 刚被分配 */
#define I_DIRTY       0x00000001  /* inode 已修改，需要写回 */
#define I_DIRTY_TIME  0x00000004  /* 时间戳需要更新 */

/**
 * inode - 索引节点结构体
 * 
 * inode 是文件系统中表示文件或目录的核心数据结构
 */
struct inode {
	/* 链表管理 */
	struct list_head	i_list;		/* 超级块 inode 链表 */
	struct list_head	i_sb_list;	/* 超级块 inode 列表 */
	
	/* 基础信息 */
	unsigned long		i_ino;		/* inode 编号 */
	unsigned int		i_nlink;	/* 硬链接计数 */
	uid_t			i_uid;		/* 用户 ID */
	gid_t			i_gid;		/* 组 ID */
	umode_t			i_mode;		/* 文件类型和权限 */
	dev_t			i_rdev;		/* 设备号（如果是设备文件） */
	
	/* 时间戳 */
	time64_t		i_atime;	/* 访问时间 */
	time64_t		i_mtime;	/* 修改时间 */
	time64_t		i_ctime;	/* 创建/状态改变时间 */
	
	/* 大小和块信息 */
	loff_t			i_size;		/* 文件大小（字节） */
	unsigned long		i_blocks;	/* 分配的块数 */
	unsigned int		i_blkbits;	/* 块大小（位） */
	
	/* 文件系统相关 */
	struct super_block	*i_sb;		/* 所属超级块 */
	const struct inode_operations *i_op;	/* inode 操作函数 */
	
	/* 状态标志 */
	unsigned long		i_state;	/* inode 状态标志 */
	unsigned long		i_flags;	/* inode 标志 */
	
	/* 引用计数 */
	int			i_count;	/* 引用计数 */
	
	/* 地址空间（用于页缓存） */
	struct address_space	*i_mapping;	/* 地址空间映射 */
	
	/* 文件系统私有数据 */
	void			*i_private;	/* 文件系统私有数据（Ext4 特定） */
};

/* 前向声明 */
struct address_space;
struct dentry;

#endif /* _LINUX_FS_INODE_H */

