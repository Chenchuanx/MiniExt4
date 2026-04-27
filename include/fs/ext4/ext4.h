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
	__le16	bg_itable_unused_hi;	/* 未使用 Inode 表项数（高 16 位） */
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

/* Inode 标志位（与 Linux ext4 兼容的 EXTENTS / DIR_INDEX 标志） */
#define EXT4_INODE_FLAG_EXTENTS 0x00080000U

/* 目录使用基于哈希的 HTree 索引（与 Linux EXT4_INDEX_FL 一致的位值） */
#define EXT4_INODE_FLAG_INDEX   0x00001000U

/* 兼容/不兼容特性标志
 *
 * - EXT4_FEATURE_COMPAT_DIR_INDEX: 目录使用基于哈希的 HTree 索引
 * - EXT4_FEATURE_INCOMPAT_EXTENTS: 文件数据使用 extents 映射
 */
#define EXT4_FEATURE_COMPAT_DIR_INDEX 0x00000020U

/* 不兼容特性标志（仅使用 EXTENTS，用于让宿主 Linux 识别 extents 模式） */
#define EXT4_FEATURE_INCOMPAT_EXTENTS 0x00000040U

/* === 目录 HTree on-disk 结构（对齐 Linux fs/ext4/htree.h） === */

/* HTree 根节点头部信息，位于目录第一个块中的 "." 目录项之后 */
struct ext4_dx_root_info {
	__le32	reserved_zero;   /* 保留，置为 0 */
	__u8	hash_version;   /* 哈希版本（与 s_def_hash_version 对应） */
	__u8	info_length;    /* 本结构体长度（以字节为单位） */
	__u8	indirect_levels;/* 间接层数：0=单层，>0 有 dx_node */
	__u8	unused_flags;   /* 当前未使用 */
} __attribute__((packed));

/* HTree entry header（Linux: dx_countlimit） */
struct ext4_dx_countlimit {
	__le16	limit;          /* entries[] 总容量 */
	__le16	count;          /* entries[] 当前条目数 */
} __attribute__((packed));

/* HTree 索引条目（root/leaf 索引块通用） */
struct ext4_dx_entry {
	__le32	hash;           /* 哈希值（高位/truncated） */
	__le32	block;          /* 逻辑块号（相对该目录起始数据块） */
} __attribute__((packed));

/* HTree 索引块头部（dx_root / dx_node 复用）
 *
 * 说明：这里不直接嵌入 struct ext4_dir_entry，以避免在本头文件中引入对其完整定义的依赖。
 * 实际使用时应在目录实现代码中，通过适当的偏移将 on-disk 布局解释为 dirent+root_info+entries。 */
struct ext4_dx_node {
	__le32	fake_inum;
	__le16	fake_rec_len;
	__u8	fake_name_len;
	__u8	fake_file_type;
	struct ext4_dx_root_info info;
	struct ext4_dx_countlimit countlimit;
	struct ext4_dx_entry entries[0];
} __attribute__((packed));

/* HTree hash version（与 Linux ext4 保持一致） */
#define EXT4_DX_HASH_LEGACY            0
#define EXT4_DX_HASH_HALF_MD4          1
#define EXT4_DX_HASH_TEA               2
#define EXT4_DX_HASH_LEGACY_UNSIGNED   3
#define EXT4_DX_HASH_HALF_MD4_UNSIGNED 4
#define EXT4_DX_HASH_TEA_UNSIGNED      5

/* Ext4 目录项（on-disk，与 Linux ext2/ext3/ext4 一致：name_len/file_type 各 1 字节） */
struct ext4_dir_entry {
	__le32	inode;		/* Inode 号 */
	__le16	rec_len;	/* 记录长度 */
	__u8	name_len;	/* 名称长度（1 字节） */
	__u8	file_type;	/* 文件类型：DT_UNKNOWN=0, DT_REG=1, DT_DIR=2 等 */
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
	/* 组描述符大小（字节）。典型值：32（老格式）、64（ext4 新格式） */
	uint16_t	s_desc_size;
	
	/* 块组描述符表（简化版，只保存第一个块组） */
	struct ext4_group_desc *s_group_desc;
	
	/* 根 Inode 号（通常是 2） */
	uint32_t	s_root_ino;

	/* 目录 HTree 哈希参数（从 superblock 读取） */
	uint32_t	s_hash_seed[4];
	uint8_t		s_def_hash_version;

	/* 块分配器状态（参考 Linux ext4 的 goal + bitmap cache 思路） */
	uint32_t	s_alloc_goal_group;      /* 无首选组时 next-fit 起始块组 */
	uint32_t	*s_alloc_goal_bit_per_group; /* 每组块位图内 next-fit 起始 bit（动态分配） */
	uint32_t	s_alloc_last_group;      /* 最近一次成功分配所在组 */
	uint32_t	s_alloc_last_bit;        /* 最近一次成功分配所在 bit */

	uint32_t	s_bmap_cache_group;      /* 当前缓存的块位图所属组 */
	uint8_t		s_bmap_cache_valid;      /* 位图缓存是否有效 */
	uint8_t		s_bmap_cache_dirty;      /* 位图缓存是否已修改 */
	char		*s_bmap_cache_buf;       /* 位图缓存数据（大小为 block_size） */

	uint32_t	s_bg_sync_pending;       /* 延迟写回 group desc 计数 */
};

/* Ext4 Inode 信息（内存结构，挂到 inode->i_private）
 *
 * 说明：
 * - 目录仍然使用传统的直接块数组 i_block[0..11] 存放目录块号；
 * - 普通文件开启 EXT4_INODE_FLAG_EXTENTS 时：
 *   - i_block 数组的 60 字节区域按照 Linux ext4 的格式解释为：
 *     ext4_extent_header + 若干 ext4_extent / ext4_extent_idx（root 节点）
 *   - 即 root extents 节点直接内嵌在 inode 中，而不是单独的索引块。
 */
struct ext4_inode_info {
	/* 从磁盘 inode 读取的信息 */
	uint32_t	i_block[15];	/* 块指针数组 */
	uint32_t	i_flags;	/* Ext4 Inode 标志 */
	uint32_t	i_alloc_group_hint; /* 数据块分配首选组（内存 hint） */

	/* 目录索引用内存结构（基于 B+Tree），非持久化 */
	void		*dir_index;	/* 指向内部目录索引根/上下文 */
};

/* === Extents 机制相关结构（对齐 Linux ext4 on-disk 布局） === */

/* 与 Linux fs/ext4/extents.h 对齐的头部 */
struct ext4_extent_header {
	__le16	eh_magic;	/* 魔数：0xF30A */
	__le16	eh_entries;	/* 当前已使用的条目数 */
	__le16	eh_max;		/* 最大条目数 */
	__le16	eh_depth;	/* 深度：0=叶子，>0 为索引节点 */
	__le32	eh_generation;	/* 代数（当前未使用） */
} __attribute__((packed));

/* 叶子节点中的 extent 条目 */
struct ext4_extent {
	__le32	ee_block;	/* 逻辑起始块号 */
	__le16	ee_len;		/* 块数 */
	__le16	ee_start_hi;	/* 物理起始块号的高 16 位 */
	__le32	ee_start_lo;	/* 物理起始块号的低 32 位 */
} __attribute__((packed));

/* 索引节点中的索引条目（当前 MiniExt4 仅预留，不必马上实现多级） */
struct ext4_extent_idx {
	__le32	ei_block;	/* 子树覆盖的最小逻辑块号 */
	__le32	ei_leaf_lo;	/* 子节点块号（低 32 位） */
	__le16	ei_leaf_hi;	/* 子节点块号（高 16 位） */
	__le16	ei_unused;
} __attribute__((packed));

/* Extents 头部魔数（与 Linux 保持一致） */
#define EXT4_EXT_MAGIC 0xF30A

/* 基于 extents 的数据块映射接口 */
int ext4_extents_get_block(struct inode *inode, uint32_t lblock,
			   int create, uint32_t *out_block, int *is_new);

/* 函数声明 */
int ext4_read_block(uint32_t blocknr, void *buf);
int ext4_write_block(uint32_t blocknr, const void *buf);
int ext4_write_blocks(uint32_t blocknr, uint32_t blocks, const void *buf);
void ext4_set_block_size(uint32_t size);
uint32_t ext4_get_block_size(void);
uint32_t ext4_get_blocks_count(void);
uint32_t ext4_get_blocks_per_group(void);

/* Ext4 文件系统初始化（格式化） */
int ext4_mkfs(uint32_t block_size, uint32_t total_blocks);

/* 块分配和释放 */
uint32_t ext4_new_block(struct super_block *sb);
uint32_t ext4_new_blocks(struct super_block *sb, uint32_t goal_len, uint32_t *out_len);
uint32_t ext4_new_block_in_group(struct super_block *sb, uint32_t preferred_group);
uint32_t ext4_new_blocks_in_group(struct super_block *sb, uint32_t goal_len,
				  uint32_t *out_len, uint32_t preferred_group);
int ext4_free_block(struct super_block *sb, uint32_t blocknr);
int ext4_balloc_flush(struct super_block *sb);

/* Inode 分配和释放 */
uint32_t ext4_new_inode(struct super_block *sb);
uint32_t ext4_new_inode_in_group(struct super_block *sb, uint32_t preferred_group);
int ext4_free_inode(struct super_block *sb, uint32_t ino);

/* 将块组描述符中的空闲块/inode 计数写回磁盘超级块（单块组镜像与 Linux 一致） */
int ext4_sync_super_free_counts(struct super_block *sb);

/* 根据 inode 号从磁盘加载 inode（供 lookup 等使用） */
struct inode *ext4_iget(struct super_block *sb, unsigned long ino);

/* 目录项查找/添加/删除（fs/ext4/dir.c） */
int ext4_find_entry(struct inode *dir, const struct qstr *name,
		    unsigned long *out_ino, uint32_t *out_blocknr, uint32_t *out_off);
int ext4_add_entry(struct inode *dir, const struct qstr *name, unsigned long ino);
int ext4_remove_entry(struct inode *dir, const struct qstr *name);
int ext4_dir_foreach(struct inode *dir, void *ctx,
		     int (*filldir)(void *ctx, const char *name, int name_len,
				   unsigned long ino, unsigned int type));

/* Ext4 inode 操作（供 ext4_iget 设置 inode->i_op） */
extern const struct inode_operations ext4_dir_inode_operations;
extern const struct inode_operations ext4_file_inode_operations;

/* Ext4 默认文件/目录操作（供 ext4_iget 设置 inode->i_fop） */
extern const struct file_operations ext4_file_operations;
extern const struct file_operations ext4_dir_operations;

/* Ext4 文件系统类型（外部声明） */
extern struct file_system_type ext4_fs_type;

#endif /* _EXT4_H */

