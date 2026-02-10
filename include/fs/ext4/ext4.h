/* 
 * Ext4 文件系统定义
 * 包含 on-disk 结构和内存结构
 */
#ifndef _EXT4_H
#define _EXT4_H

#include <linux/types.h>
#include <linux/fs.h>

/* Ext4 魔数 */
#define EXT4_SUPER_MAGIC 0xEF53

/* 块大小相关 */
#define EXT4_MIN_BLOCK_SIZE     1024
#define EXT4_MAX_BLOCK_SIZE     65536
#define EXT4_MIN_BLOCK_LOG_SIZE 10

/* __le32 / __le16 / __u8 类型定义（小端字节序） */
typedef uint32_t __le32;
typedef uint16_t __le16;
typedef uint64_t __le64;
typedef uint8_t __u8;

/* Ext4 超级块（on-disk，偏移 1024 字节） */
struct ext4_super_block {
	__le32	s_inodes_count;		/* Inodes 总数 */
	__le32	s_blocks_count_lo;	/* 块总数（低 32 位） */
	__le32	s_r_blocks_count_lo;	/* 保留块数（低 32 位） */
	__le32	s_free_blocks_count_lo;	/* 空闲块数（低 32 位） */
	__le32	s_free_inodes_count;	/* 空闲 Inodes 数 */
	__le32	s_first_data_block;	/* 第一个数据块 */
	__le32	s_log_block_size;	/* 块大小 = 1024 << s_log_block_size */
	__le32	s_log_cluster_size;	/* 簇大小（未使用） */
	__le32	s_blocks_per_group;	/* 每组块数 */
	__le32	s_clusters_per_group;	/* 每组簇数 */
	__le32	s_inodes_per_group;	/* 每组 Inodes 数 */
	__le32	s_mtime;		/* 挂载时间 */
	__le32	s_wtime;		/* 写入时间 */
	__le16	s_mnt_count;		/* 挂载计数 */
	__le16	s_max_mnt_count;	/* 最大挂载计数 */
	__le16	s_magic;		/* 魔数 0xEF53 */
	__le16	s_state;		/* 文件系统状态 */
	__le16	s_errors;		/* 错误处理行为 */
	__le16	s_minor_rev_level;	/* 次版本号 */
	__le32	s_lastcheck;		/* 最后检查时间 */
	__le32	s_checkinterval;	/* 检查间隔 */
	__le32	s_creator_os;		/* 创建者 OS */
	__le32	s_rev_level;		/* 修订级别 */
	__le16	s_def_resuid;		/* 默认保留 UID */
	__le16	s_def_resgid;		/* 默认保留 GID */
	__le32	s_first_ino;		/* 第一个非保留 Inode */
	__le16	s_inode_size;		/* Inode 大小 */
	__le16	s_block_group_nr;	/* 块组编号 */
	__le32	s_feature_compat;	/* 兼容特性 */
	__le32	s_feature_incompat;	/* 不兼容特性 */
	__le32	s_feature_ro_compat;	/* 只读兼容特性 */
	__u8	s_uuid[16];		/* 128 位 UUID */
	char	s_volume_name[16];	/* 卷名 */
	char	s_last_mounted[64];	/* 最后挂载路径 */
	__le32	s_algorithm_usage_bitmap; /* 压缩算法 */
	__u8	s_prealloc_blocks;	/* 预分配块数 */
	__u8	s_prealloc_dir_blocks;	/* 目录预分配块数 */
	__le16	s_reserved_gdt_blocks;	/* 保留 GDT 块数 */
	__u8	s_journal_uuid[16];	/* 日志 UUID */
	__le32	s_journal_inum;		/* 日志 Inode 号 */
	__le32	s_journal_dev;		/* 日志设备号 */
	__le32	s_last_orphan;		/* 最后孤儿 Inode */
	__le32	s_hash_seed[4];		/* 哈希种子 */
	__u8	s_def_hash_version;	/* 默认哈希版本 */
	__u8	s_jnl_backup_type;	/* 日志备份类型 */
	__le16	s_desc_size;		/* 组描述符大小 */
	__le32	s_default_mount_opts;	/* 默认挂载选项 */
	__le32	s_first_meta_bg;	/* 第一个元数据块组 */
	__le32	s_mkfs_time;		/* mkfs 时间 */
	__le32	s_jnl_blocks[17];	/* 日志块 */
	__le32	s_blocks_count_hi;	/* 块总数（高 32 位） */
	__le32	s_r_blocks_count_hi;	/* 保留块数（高 32 位） */
	__le32	s_free_blocks_count_hi;	/* 空闲块数（高 32 位） */
	__le16	s_min_extra_isize;	/* 最小额外 Inode 大小 */
	__le16	s_want_extra_isize;	/* 期望额外 Inode 大小 */
	__le32	s_flags;		/* 标志 */
	__le16	s_raid_stride;		/* RAID 步长 */
	__le16	s_mmp_interval;		/* 多挂载保护间隔 */
	__le64	s_mmp_block;		/* 多挂载保护块 */
	__le32	s_raid_stripe_width;	/* RAID 条带宽度 */
	__u8	s_log_groups_per_flex;	/* Flex 块组 */
	__u8	s_checksum_type;	/* 校验和类型 */
	__u8	s_encryption_level;	/* 加密级别 */
	__u8	s_reserved_pad;		/* 保留填充 */
	__le64	s_kbytes_written;	/* 写入 KB 数 */
	__le32	s_snapshot_inum;	/* 快照 Inode 号 */
	__le32	s_snapshot_id;		/* 快照 ID */
	__le64	s_snapshot_r_blocks_count; /* 快照保留块数 */
	__le32	s_snapshot_list;	/* 快照列表 */
	__le32	s_error_count;		/* 错误计数 */
	__le32	s_first_error_time;	/* 首次错误时间 */
	__le32	s_first_error_ino;	/* 首次错误 Inode */
	__le64	s_first_error_block;	/* 首次错误块 */
	__u8	s_first_error_func[32];	/* 首次错误函数 */
	__le32	s_first_error_line;	/* 首次错误行号 */
	__le32	s_last_error_time;	/* 最后错误时间 */
	__le32	s_last_error_ino;	/* 最后错误 Inode */
	__le32	s_last_error_line;	/* 最后错误行号 */
	__le64	s_last_error_block;	/* 最后错误块 */
	__u8	s_last_error_func[32];	/* 最后错误函数 */
	__u8	s_mount_opts[64];	/* 挂载选项 */
	__le32	s_usr_quota_inum;	/* 用户配额 Inode */
	__le32	s_grp_quota_inum;	/* 组配额 Inode */
	__le32	s_overhead_blocks;	/* 开销块数 */
	__le32	s_backup_bgs[2];	/* 备份块组 */
	__u8	s_encrypt_algos[4];	/* 加密算法 */
	__u8	s_encrypt_pw_salt[16];	/* 加密密码盐 */
	__le32	s_lpf_ino;		/* 丢失+找到 Inode */
	__le32	s_projection;		/* 投影 */
	__le32	s_prj_quota_inum;	/* 项目配额 Inode */
	__le32	s_checksum_seed;	/* 校验和种子 */
	__u8	s_wtime_hi;		/* 写入时间（高 8 位） */
	__u8	s_mtime_hi;		/* 挂载时间（高 8 位） */
	__u8	s_mkfs_time_hi;		/* mkfs 时间（高 8 位） */
	__u8	s_lastcheck_hi;		/* 最后检查时间（高 8 位） */
	__u8	s_first_error_time_hi;	/* 首次错误时间（高 8 位） */
	__u8	s_last_error_time_hi;	/* 最后错误时间（高 8 位） */
	__u8	s_pad[2];		/* 填充 */
	__le16	s_encoding;		/* 文件名编码 */
	__le16	s_encoding_flags;	/* 编码标志 */
	__u8	s_orphan_file_inum_list[128]; /* 孤儿文件 Inode 列表 */
	__le32	s_reserved[160];	/* 保留字段 */
} __attribute__((packed));

/* Ext4 块组描述符 */
struct ext4_group_desc {
	__le32	bg_block_bitmap_lo;	/* 块位图块号（低 32 位） */
	__le32	bg_inode_bitmap_lo;	/* Inode 位图块号（低 32 位） */
	__le32	bg_inode_table_lo;	/* Inode 表块号（低 32 位） */
	__le16	bg_free_blocks_count_lo; /* 空闲块数（低 16 位） */
	__le16	bg_free_inodes_count_lo; /* 空闲 Inodes 数（低 16 位） */
	__le16	bg_used_dirs_count_lo;	/* 已用目录数（低 16 位） */
	__le16	bg_flags;		/* 块组标志 */
	__le32	bg_exclude_bitmap_lo;	/* 排除位图块号 */
	__le16	bg_block_bitmap_csum_lo; /* 块位图校验和（低 16 位） */
	__le16	bg_inode_bitmap_csum_lo; /* Inode 位图校验和（低 16 位） */
	__le16	bg_itable_unused_lo;	/* 未使用 Inode 表项数（低 16 位） */
	__le16	bg_checksum;		/* 组描述符校验和 */
	__le32	bg_block_bitmap_hi;	/* 块位图块号（高 32 位） */
	__le32	bg_inode_bitmap_hi;	/* Inode 位图块号（高 32 位） */
	__le32	bg_inode_table_hi;	/* Inode 表块号（高 32 位） */
	__le16	bg_free_blocks_count_hi; /* 空闲块数（高 16 位） */
	__le16	bg_free_inodes_count_hi; /* 空闲 Inodes 数（高 16 位） */
	__le16	bg_used_dirs_count_hi;	/* 已用目录数（高 16 位） */
	__le32	bg_itable_unused_hi;	/* 未使用 Inode 表项数（高 32 位） */
	__le32	bg_exclude_bitmap_hi;	/* 排除位图块号（高 32 位） */
	__le16	bg_block_bitmap_csum_hi; /* 块位图校验和（高 16 位） */
	__le16	bg_inode_bitmap_csum_hi; /* Inode 位图校验和（高 16 位） */
	__le32	bg_reserved;		/* 保留 */
} __attribute__((packed));

/* Ext4 Inode（on-disk） */
struct ext4_inode {
	__le16	i_mode;		/* 文件类型和权限 */
	__le16	i_uid;		/* 用户 ID（低 16 位） */
	__le32	i_size_lo;	/* 文件大小（低 32 位） */
	__le32	i_atime;	/* 访问时间 */
	__le32	i_ctime;	/* 创建时间 */
	__le32	i_mtime;	/* 修改时间 */
	__le32	i_dtime;	/* 删除时间 */
	__le16	i_gid;		/* 组 ID（低 16 位） */
	__le16	i_links_count;	/* 硬链接计数 */
	__le32	i_blocks_lo;	/* 块数（低 32 位，512 字节单位） */
	__le32	i_flags;	/* Inode 标志 */
	__le32	i_osd1;		/* OS 依赖 1 */
	__le32	i_block[15];	/* 块指针（12 个直接 + 1 个间接 + 1 个双重间接 + 1 个三重间接） */
	__le32	i_generation;	/* 文件版本（用于 NFS） */
	__le32	i_file_acl_lo;	/* 文件 ACL（低 32 位） */
	__le32	i_size_high;	/* 文件大小（高 32 位） */
	__le32	i_obso_faddr;	/* 废弃的片段地址 */
	__le16	i_obso_osd2;	/* 废弃的 OS 依赖 2 */
	__le16	i_blocks_hi;	/* 块数（高 16 位） */
	__le32	i_file_acl_high; /* 文件 ACL（高 32 位） */
	__le32	i_uid_high;	/* 用户 ID（高 16 位） */
	__le32	i_gid_high;	/* 组 ID（高 16 位） */
	__le32	i_checksum_lo;	/* Inode 校验和（低 16 位） */
	__le32	i_reserved2;	/* 保留 */
} __attribute__((packed));

/* Ext4 目录项（on-disk） */
struct ext4_dir_entry {
	__le32	inode;		/* Inode 号 */
	__le16	rec_len;	/* 记录长度 */
	__le16	name_len;	/* 名称长度 */
	char	name[255];	/* 文件名（可变长度） */
} __attribute__((packed));

/* Ext4 超级块信息（内存结构，挂到 sb->s_fs_info） */
struct ext4_sb_info {
	/* 从磁盘 superblock 读取的信息 */
	uint32_t	s_blocks_per_group;	/* 每组块数 */
	uint32_t	s_inodes_per_group;	/* 每组 Inodes 数 */
	uint32_t	s_inodes_count;		/* Inodes 总数 */
	uint32_t	s_blocks_count;		/* 块总数 */
	uint32_t	s_first_data_block;	/* 第一个数据块 */
	uint32_t	s_log_block_size;	/* 块大小对数 */
	uint32_t	s_block_size;		/* 块大小（字节） */
	uint32_t	s_groups_count;		/* 块组数 */
	
	/* 块组描述符表（简化版，只保存第一个块组） */
	struct ext4_group_desc *s_group_desc;
	
	/* 根 Inode 号（通常是 2） */
	uint32_t	s_root_ino;
};

/* Ext4 Inode 信息（内存结构，挂到 inode->i_private） */
struct ext4_inode_info {
	/* 从磁盘 inode 读取的信息 */
	uint32_t	i_block[15];	/* 块指针数组 */
	uint32_t	i_flags;	/* Ext4 Inode 标志 */
};

/* 函数声明 */
int ext4_read_block(uint32_t blocknr, void *buf);
int ext4_write_block(uint32_t blocknr, const void *buf);
void ext4_set_block_size(uint32_t size);
uint32_t ext4_get_block_size(void);

/* Ext4 文件系统初始化（格式化） */
int ext4_mkfs(uint32_t block_size, uint32_t total_blocks);

/* 块分配和释放 */
uint32_t ext4_new_block(struct super_block *sb);
int ext4_free_block(struct super_block *sb, uint32_t blocknr);

/* Ext4 文件系统类型（外部声明） */
extern struct file_system_type ext4_fs_type;

#endif /* _EXT4_H */

