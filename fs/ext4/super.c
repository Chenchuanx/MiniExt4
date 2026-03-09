/* 
 * Ext4 超级块和挂载实现
 */
#include <linux/fs.h>
#include <fs/fs_types.h>
#include <fs/ext4/ext4.h>
#include <lib/printf.h>
#include <fs/dentry.h>  /* 使用 VFS dentry 接口 */

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 前向声明 */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern void ext4_set_block_size(uint32_t size);

/* 简化的内存操作函数（避免依赖标准库） */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) {
		p[i] = (unsigned char)c;
	}
	return s;
}

static void *simple_memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	size_t i;
	for (i = 0; i < n; i++) {
		d[i] = s[i];
	}
	return dest;
}

#define memset simple_memset
#define memcpy simple_memcpy

/* 简化的内存分配（使用静态池，避免依赖标准库） */
#define MAX_MALLOC_SIZE 4096
#define MAX_MALLOC_BLOCKS 32
static char malloc_pool[MAX_MALLOC_BLOCKS][MAX_MALLOC_SIZE];
static int malloc_used[MAX_MALLOC_BLOCKS];

static void *simple_malloc(size_t size)
{
	int i;
	if (size > MAX_MALLOC_SIZE) {
		return NULL;
	}
	for (i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (!malloc_used[i]) {
			malloc_used[i] = 1;
			return malloc_pool[i];
		}
	}
	return NULL;
}

static void simple_free(void *ptr)
{
	int i;
	if (!ptr) return;
	for (i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (malloc_pool[i] == ptr) {
			malloc_used[i] = 0;
			return;
		}
	}
}

#define malloc simple_malloc
#define free simple_free

/* 前向声明 */
static struct dentry *ext4_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data);
static void ext4_kill_sb(struct super_block *sb);
static struct inode *ext4_alloc_inode(struct super_block *sb);
static void ext4_destroy_inode(struct inode *inode);
static int ext4_fill_super(struct super_block *sb, void *data);
static int ext4_write_inode(struct inode *inode, struct writeback_control *wbc);

/* Ext4 的 super_operations */
static const struct super_operations ext4_sops = {
    ext4_alloc_inode,      /* alloc_inode */
    ext4_destroy_inode,    /* destroy_inode */
    NULL,                  /* put_super */
    NULL,                  /* sync_fs */
    NULL,                  /* statfs */
    NULL,                  /* remount_fs */
    NULL,                  /* umount_begin */
    NULL,                   /* dirty_inode */
    ext4_write_inode,      /* write_inode */
    NULL,                  /* evict_inode */
};

/* Ext4 文件系统类型描述 */
struct file_system_type ext4_fs_type = {
    "ext4",
    0,
    ext4_mount,
    ext4_kill_sb,
    { NULL, NULL },
    NULL,
    0,  /* false */
    NULL,
};

/* 注意：dentry 分配现在由 VFS 层的 d_alloc() 统一管理 */

/**
 * ext4_alloc_inode - 分配 Ext4 inode
 */
static struct inode *ext4_alloc_inode(struct super_block *sb)
{
    struct inode *inode = vfs_alloc_inode(sb);
    if (!inode) {
        return NULL;
    }
    
    /* 分配 Ext4 私有数据 */
    struct ext4_inode_info *ei = (struct ext4_inode_info *)malloc(sizeof(*ei));
    if (!ei) {
        vfs_free_inode(inode);
        return NULL;
    }
    memset(ei, 0, sizeof(*ei));
    inode->i_private = ei;
    
    return inode;
}

/**
 * ext4_destroy_inode - 销毁 Ext4 inode
 */
static void ext4_destroy_inode(struct inode *inode)
{
    if (inode && inode->i_private) {
        free(inode->i_private);
        inode->i_private = NULL;
    }
    vfs_free_inode(inode);
}

/**
 * ext4_mkfs - 初始化 Ext4 文件系统（格式化）
 * 
 * 在空白磁盘上创建 Ext4 文件系统结构
 */
int ext4_mkfs(uint32_t block_size, uint32_t total_blocks)
{
    char *buf;
    struct ext4_super_block *esb;
    struct ext4_group_desc *gd;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t groups_count;
    uint32_t first_data_block;
    uint32_t inode_size = 256;  /* Ext4 默认 inode 大小 256 字节 */
    int ret;
    
    /* 设置块大小 */
    ext4_set_block_size(block_size);
    
    /* 计算文件系统参数 */
    /* 对于小文件系统，blocks_per_group 不能超过 total_blocks */
    /* 标准 ext4 使用 32768，但对于小文件系统需要调整 */
    if (total_blocks < 32768) {
        blocks_per_group = total_blocks;  /* 小文件系统：所有块在一个组 */
    } else {
        blocks_per_group = 32768;  /* 标准：每组 32768 块 */
    }
    inodes_per_group = blocks_per_group / 4;  /* 简化：每 4 块一个 inode */
    groups_count = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    first_data_block = (block_size == 1024) ? 1 : 0;
    
    /* 分配缓冲区（至少 4096 字节，确保能容纳整个块和超级块） */
    uint32_t buf_size = (block_size > 4096) ? block_size : 4096;
    buf = (char *)malloc(buf_size);
    if (!buf) {
        return -1;
    }
    memset(buf, 0, buf_size);
    
    /* 读取块 0（包含 boot sector 和 superblock） */
    ret = ext4_read_block(0, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 初始化 superblock（偏移 1024 字节） */
    esb = (struct ext4_super_block *)(buf + 1024);
    memset(esb, 0, sizeof(*esb));
    
    /* 填充 superblock 基本字段 */
    esb->s_inodes_count = groups_count * inodes_per_group;
    esb->s_blocks_count_lo = total_blocks;
    esb->s_r_blocks_count_lo = total_blocks / 20;  /* 保留 5% 的块 */
    esb->s_free_inodes_count = esb->s_inodes_count - 10;  /* 减去系统 inode */
    /* s_free_blocks_count_lo 会在后面根据实际计算的系统块数更新 */
    esb->s_first_data_block = first_data_block;
    esb->s_log_block_size = (block_size == 1024) ? 0 : (block_size == 2048) ? 1 : 2;  /* 简化 */
    /* s_log_cluster_size 应等于 s_log_block_size */
    esb->s_log_cluster_size = esb->s_log_block_size;
    esb->s_blocks_per_group = blocks_per_group;
    esb->s_clusters_per_group = blocks_per_group;
    esb->s_inodes_per_group = inodes_per_group;
    /* 设置时间戳（使用一个合理的固定值，Unix 时间戳，大约 2020-01-01） */
    uint32_t base_time = 0x5E0D9800;  /* 2020-01-01 00:00:00 UTC */
    esb->s_mtime = base_time;  /* 挂载时间 */
    esb->s_wtime = base_time;  /* 写入时间 */
    esb->s_mnt_count = 0;
    esb->s_max_mnt_count = 0xFFFF;  /* 无限制 */
    esb->s_magic = EXT4_SUPER_MAGIC;
    esb->s_state = 1;  /* 文件系统干净 */
    esb->s_errors = 1;  /* 继续 */
    esb->s_minor_rev_level = 0;
    esb->s_lastcheck = base_time;  /* 最后检查时间 */
    esb->s_checkinterval = 0;  /* 不强制检查 */
    esb->s_creator_os = 0;  /* Linux */
    esb->s_rev_level = 1;  /* 动态版本 */
    esb->s_def_resuid = 0;
    esb->s_def_resgid = 0;
    esb->s_first_ino = 11;  /* 第一个非保留 inode */
    esb->s_inode_size = inode_size;
    esb->s_block_group_nr = 0;
    esb->s_feature_compat = 0;
    esb->s_feature_incompat = 0;
    esb->s_feature_ro_compat = 0;
    esb->s_mkfs_time = base_time;  /* mkfs 时间 */
    esb->s_journal_inum = 0;  /* 无日志 */
    esb->s_journal_dev = 0;  /* 无日志设备 */
    esb->s_last_orphan = 0;  /* 无孤儿 inode */
    esb->s_hash_seed[0] = 0;
    esb->s_hash_seed[1] = 0;
    esb->s_hash_seed[2] = 0;
    esb->s_hash_seed[3] = 0;
    esb->s_def_hash_version = 0;  /* 默认哈希版本 */
    esb->s_jnl_backup_type = 0;  /* 无日志备份 */
    esb->s_desc_size = 32;  /* 组描述符大小：32 字节 */
    
    /* 生成简单的 UUID */
    for (int i = 0; i < 16; i++) {
        esb->s_uuid[i] = (uint8_t)(i * 17);
    }
    
    /* 设置卷名和最后挂载路径 */
    memset(esb->s_volume_name, 0, sizeof(esb->s_volume_name));
    memset(esb->s_last_mounted, 0, sizeof(esb->s_last_mounted));
    
    /* 设置高位字段（小文件系统应为 0） */
    esb->s_blocks_count_hi = 0;
    esb->s_r_blocks_count_hi = 0;
    esb->s_free_blocks_count_hi = 0;
    
    /* 计算系统块数和空闲块数 */
    uint32_t inode_table_blocks_precalc = (inodes_per_group * inode_size + block_size - 1) / block_size;
    uint32_t group0_inode_table_precalc;
    if (block_size == 1024) {
        group0_inode_table_precalc = 5;
    } else {
        group0_inode_table_precalc = 4;
    }
    uint32_t root_dir_block_precalc = group0_inode_table_precalc + inode_table_blocks_precalc;
    uint32_t system_blocks_precalc = root_dir_block_precalc + 1;
    if (system_blocks_precalc > blocks_per_group) {
        system_blocks_precalc = blocks_per_group;
    }
    uint32_t group_free_blocks_precalc = blocks_per_group - system_blocks_precalc;
    
    /* 更新超级块中的空闲块数 */
    if (groups_count == 1) {
        if (group_free_blocks_precalc > esb->s_r_blocks_count_lo) {
            esb->s_free_blocks_count_lo = group_free_blocks_precalc - esb->s_r_blocks_count_lo;
        } else {
            esb->s_free_blocks_count_lo = 0;
        }
    } else {
        esb->s_free_blocks_count_lo = total_blocks - esb->s_r_blocks_count_lo - system_blocks_precalc;
    }
    
    /* 写入 superblock */
    ret = ext4_write_block(0, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 初始化第一个块组的 group descriptor */
    memset(buf, 0, block_size);
    gd = (struct ext4_group_desc *)buf;
    
    /* 计算第一个块组的布局 */
    /* 对于 4KB 块：块 1 = group descriptor, 块 2 = block bitmap, 块 3 = inode bitmap, 块 4 = inode table */
    /* 对于 1KB 块：块 2 = group descriptor, 块 3 = block bitmap, 块 4 = inode bitmap, 块 5 = inode table */
    uint32_t group0_block_bitmap, group0_inode_bitmap, group0_inode_table;
    if (block_size == 1024) {
        group0_block_bitmap = 3;
        group0_inode_bitmap = 4;
        group0_inode_table = 5;
    } else {
        group0_block_bitmap = 2;  /* 4KB 块，block bitmap 在块 2 */
        group0_inode_bitmap = 3;  /* inode bitmap 在块 3 */
        group0_inode_table = 4;   /* inode table 在块 4 */
    }
    uint32_t inode_table_blocks = (inodes_per_group * inode_size + block_size - 1) / block_size;
    
    gd->bg_block_bitmap_lo = group0_block_bitmap;
    gd->bg_inode_bitmap_lo = group0_inode_bitmap;
    gd->bg_inode_table_lo = group0_inode_table;
    /* 计算实际使用的系统块数 */
    uint32_t root_dir_block = group0_inode_table + inode_table_blocks;
    uint32_t system_blocks = root_dir_block + 1;
    if (system_blocks > blocks_per_group) {
        system_blocks = blocks_per_group;
    }
    uint32_t group_free_blocks = blocks_per_group - system_blocks;
    gd->bg_free_blocks_count_lo = group_free_blocks;
    gd->bg_free_inodes_count_lo = inodes_per_group - 10;
    gd->bg_used_dirs_count_lo = 1;
    gd->bg_flags = 0;
    gd->bg_exclude_bitmap_lo = 0;
    gd->bg_block_bitmap_csum_lo = 0;  /* 简化：不计算校验和 */
    gd->bg_inode_bitmap_csum_lo = 0;  /* 简化：不计算校验和 */
    gd->bg_itable_unused_lo = inodes_per_group - 10;
    gd->bg_checksum = 0;  /* 简化：不计算校验和 */
    /* 设置高位字段（小文件系统应为 0） */
    gd->bg_block_bitmap_hi = 0;
    gd->bg_inode_bitmap_hi = 0;
    gd->bg_inode_table_hi = 0;
    gd->bg_free_blocks_count_hi = 0;
    gd->bg_free_inodes_count_hi = 0;
    gd->bg_used_dirs_count_hi = 0;
    gd->bg_itable_unused_hi = 0;
    gd->bg_exclude_bitmap_hi = 0;
    gd->bg_block_bitmap_csum_hi = 0;
    gd->bg_inode_bitmap_csum_hi = 0;
    gd->bg_reserved = 0;
    
    /* 写入 group descriptor */
    uint32_t gd_block;
    if (block_size == 1024) {
        gd_block = 2;  /* 1KB 块大小，描述符在块 2 */
    } else {
        gd_block = first_data_block + 1;  /* 4KB 块，描述符在块 1 */
    }
    ret = ext4_write_block(gd_block, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 初始化 block bitmap（全 0，表示所有块空闲） */
    memset(buf, 0, block_size);
    /* 标记系统块为已使用 */
    /* 系统块包括：块0（超级块）、块1（组描述符）、块2（块位图）、块3（inode位图）、
     * 块4开始（inode表）、以及根目录数据块 */
    uint32_t last_system_block = root_dir_block;  /* 根目录数据块是最后一个系统块 */
    for (uint32_t i = 0; i <= last_system_block; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if (byte < block_size) {
            buf[byte] |= (1 << bit);
        }
    }
    ret = ext4_write_block(group0_block_bitmap, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 初始化 inode bitmap（前 10 个 inode 保留） */
    memset(buf, 0, block_size);
    for (int i = 0; i < 10; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        buf[byte] |= (1 << bit);
    }
    ret = ext4_write_block(group0_inode_bitmap, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 初始化 inode table（创建根目录 inode，inode 2） */
    memset(buf, 0, block_size);
    struct ext4_inode *root_inode = (struct ext4_inode *)(buf + (2 - 1) * inode_size);
    root_inode->i_mode = 0x41ED;  /* 目录，权限 755 */
    root_inode->i_uid = 0;
    root_inode->i_size_lo = block_size;
    root_inode->i_size_high = 0;
    root_inode->i_atime = base_time;
    root_inode->i_ctime = base_time;
    root_inode->i_mtime = base_time;
    root_inode->i_dtime = 0;
    root_inode->i_gid = 0;
    root_inode->i_links_count = 2;  /* . 和 .. */
    root_inode->i_blocks_lo = block_size / 512;  /* 以 512 字节为单位 */
    root_inode->i_blocks_hi = 0;
    root_inode->i_flags = 0;
    root_inode->i_osd1 = 0;
    root_inode->i_generation = 0;
    root_inode->i_file_acl_lo = 0;
    root_inode->i_file_acl_high = 0;
    root_inode->i_uid_high = 0;
    root_inode->i_gid_high = 0;
    root_inode->i_obso_faddr = 0;
    root_inode->i_obso_osd2 = 0;
    root_inode->i_checksum_lo = 0;
    root_inode->i_reserved2 = 0;
    /* 初始化所有块指针为 0 */
    for (int i = 0; i < 15; i++) {
        root_inode->i_block[i] = 0;
    }
    root_inode->i_block[0] = group0_inode_table + inode_table_blocks;
    
    ret = ext4_write_block(group0_inode_table, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 初始化根目录数据块（包含 . 和 .. 条目） */
    memset(buf, 0, block_size);
    struct ext4_dir_entry *de = (struct ext4_dir_entry *)buf;
    
    /* . 条目 */
    uint16_t name_len_1 = 1;
    uint16_t rec_len_1 = (8 + name_len_1 + 3) & ~3;  /* 对齐到 4 字节 */
    de->inode = 2;
    de->rec_len = rec_len_1;
    de->name_len = name_len_1;
    de->name[0] = '.';
    
    /* .. 条目 */
    de = (struct ext4_dir_entry *)((char *)de + rec_len_1);
    uint16_t name_len_2 = 2;
    uint16_t rec_len_2 = ((block_size - rec_len_1) / 4) * 4;  /* 剩余空间，对齐到 4 字节 */
    de->inode = 2;
    de->rec_len = rec_len_2;
    de->name_len = name_len_2;
    de->name[0] = '.';
    de->name[1] = '.';
    
    ret = ext4_write_block(group0_inode_table + inode_table_blocks, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    free(buf);
    return 0;
}

/**
 * ext4_fill_super - 填充超级块
 * 
 * 从磁盘读取 Ext4 superblock，填充 VFS super_block 结构
 * 如果磁盘是空的，先初始化文件系统
 */
static int ext4_fill_super(struct super_block *sb, void *data)
{
    struct ext4_super_block *esb;
    struct ext4_sb_info *sbi;
    char *buf;
    uint32_t block_size = 4096;  /* 默认 4KB */
    int ret;
    
    (void)data;
    
    /* 分配临时缓冲区（读取第一个块，包含 superblock） */
    buf = (char *)malloc(4096);  /* 假设最大块大小 4KB */
    if (!buf) {
        return -1;
    }
    
    /* 先读取块 0（假设 superblock 在块 0，实际可能在块 1） */
    ext4_set_block_size(block_size);
    ret = ext4_read_block(0, buf);  /* 读取块 0 */
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 解析 superblock（偏移 1024 字节） */
    esb = (struct ext4_super_block *)(buf + 1024);
    
    /* 检查魔数，如果不存在则初始化文件系统 */
    if (esb->s_magic != EXT4_SUPER_MAGIC) {
        printf("Disk is empty, initializing Ext4 filesystem...\n");
        /* 磁盘是空的，先初始化 Ext4 文件系统 */
        /* 假设磁盘大小为 64MB = 16384 个 4KB 块 */
        uint32_t total_blocks = 16384;
        ret = ext4_mkfs(block_size, total_blocks);
        if (ret < 0) {
            printf("ext4_mkfs failed\n");
            free(buf);
            return -1;
        }
        printf("Ext4 filesystem initialized\n");
        
        /* 重新读取 superblock */
        ret = ext4_read_block(0, buf);
        if (ret < 0) {
            free(buf);
            return -1;
        }
        esb = (struct ext4_super_block *)(buf + 1024);
    }
    
    /* 计算块大小 */
    block_size = 1024 << esb->s_log_block_size;
    if (block_size < EXT4_MIN_BLOCK_SIZE || block_size > EXT4_MAX_BLOCK_SIZE) {
        free(buf);
        return -1;
    }
    
    /* 设置块大小 */
    ext4_set_block_size(block_size);
    
    /* 分配 Ext4 私有数据 */
    sbi = (struct ext4_sb_info *)malloc(sizeof(*sbi));
    if (!sbi) {
        free(buf);
        return -1;
    }
    memset(sbi, 0, sizeof(*sbi));
    
    /* 填充 sbi */
    sbi->s_blocks_per_group = esb->s_blocks_per_group;
    sbi->s_inodes_per_group = esb->s_inodes_per_group;
    sbi->s_inodes_count = esb->s_inodes_count;
    sbi->s_blocks_count = esb->s_blocks_count_lo;
    sbi->s_first_data_block = esb->s_first_data_block;
    sbi->s_log_block_size = esb->s_log_block_size;
    sbi->s_block_size = block_size;
    sbi->s_root_ino = 2;  /* Ext4 根目录通常是 inode 2 */
    
    /* 计算块组数 */
    sbi->s_groups_count = (sbi->s_blocks_count + sbi->s_blocks_per_group - 1) / sbi->s_blocks_per_group;
    
    /* 读取第一个块组描述符（简化版，只读第一个） */
    if (sbi->s_groups_count > 0) {
        uint32_t group_desc_block = sbi->s_first_data_block + 1;
        if (block_size == 1024) {
            group_desc_block = 2;  /* 1KB 块大小，描述符在块 2 */
        }
        
        ret = ext4_read_block(group_desc_block, buf);
        if (ret < 0) {
            printf("Failed to read group descriptor\n");
            free(sbi);
            free(buf);
            return -1;
        }
        
        sbi->s_group_desc = (struct ext4_group_desc *)malloc(sizeof(struct ext4_group_desc));
        if (!sbi->s_group_desc) {
            free(sbi);
            free(buf);
            return -1;
        }
        memcpy(sbi->s_group_desc, buf, sizeof(struct ext4_group_desc));
    }
    
    free(buf);
    
    /* 填充 VFS super_block */
    sb->s_magic = EXT4_SUPER_MAGIC;
    sb->s_blocksize = block_size;
    sb->s_blocksize_bits = esb->s_log_block_size + 10;
    sb->s_maxbytes = ((loff_t)1 << 60) - 1;  /* 简化版 */
    sb->s_op = &ext4_sops;  /* 设置操作函数表 */
    sb->s_flags = 0;
    sb->s_fs_info = sbi;    /* 设置文件系统私有信息 */
    
    /* 复制 UUID */
    memcpy(sb->s_uuid.b, esb->s_uuid, 16);
    
    /* 创建根 dentry 和根 inode（VFS 层需要） */
    struct inode *root_inode = ext4_iget(sb, sbi->s_root_ino);
    if (!root_inode) {
        printf("ext4_iget failed for root inode\n");
        free(sbi);
        return -1;
    }
    
    /* 使用 VFS 的 d_alloc 创建根 dentry */
    struct qstr root_name;
    qstr_init(&root_name, "/", 1);
    struct dentry *root_dentry = d_alloc(NULL, &root_name);
    if (!root_dentry) {
        printf("d_alloc failed for root dentry\n");
        vfs_free_inode(root_inode);
        free(sbi);
        return -1;
    }
    
    /* 关联 dentry 和 inode */
    d_instantiate(root_dentry, root_inode);
    root_dentry->d_sb = sb;
    root_dentry->d_parent = root_dentry;  /* 根目录的父目录是自己 */
    
    /* 设置到超级块 */
    sb->s_root = root_dentry;
    
    return 0;
}

/**
 * ext4_iget - 获取 inode（从磁盘读取）
 */
struct inode *ext4_iget(struct super_block *sb, unsigned long ino)
{
    struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
    struct ext4_inode_info *ei;
    struct ext4_inode *raw_inode;
    struct inode *inode;
    uint32_t block_size = sbi->s_block_size;
    uint32_t inodes_per_group = sbi->s_inodes_per_group;
    uint32_t group = (ino - 1) / inodes_per_group;
    uint32_t index = (ino - 1) % inodes_per_group;
    uint32_t inode_table_block;
    uint32_t inode_offset;
    char *buf;
    int ret;
    
    if (!sbi || !sbi->s_group_desc) {
        return NULL;
    }
    
    /* 计算 inode 表块号 */
    inode_table_block = sbi->s_group_desc->bg_inode_table_lo;
    /* 使用 superblock 中的 inode 大小（从 ext4_mkfs 中设置） */
    uint32_t inode_size = 256;  /* Ext4 固定 256 字节 */
    inode_offset = index * inode_size;
    inode_table_block += inode_offset / block_size;
    inode_offset %= block_size;
    
    /* 分配缓冲区 */
    buf = (char *)malloc(block_size);
    if (!buf) {
        return NULL;
    }
    
    /* 读取包含 inode 的块 */
    ret = ext4_read_block(inode_table_block, buf);
    if (ret < 0) {
        printf("Failed to read inode table\n");
        free(buf);
        return NULL;
    }
    
    raw_inode = (struct ext4_inode *)(buf + inode_offset);
    
    /* 分配 VFS inode */
    inode = ext4_alloc_inode(sb);
    if (!inode) {
        free(buf);
        return NULL;
    }
    
    ei = (struct ext4_inode_info *)inode->i_private;
    
    /* 填充 VFS inode */
    inode->i_ino = ino;
    inode->i_mode = raw_inode->i_mode;
    inode->i_size = raw_inode->i_size_lo | ((uint64_t)raw_inode->i_size_high << 32);
    inode->i_blocks = raw_inode->i_blocks_lo | ((uint64_t)raw_inode->i_blocks_hi << 32);
    inode->i_atime = raw_inode->i_atime;
    inode->i_mtime = raw_inode->i_mtime;
    inode->i_ctime = raw_inode->i_ctime;
    inode->i_uid = raw_inode->i_uid | ((uint32_t)raw_inode->i_uid_high << 16);
    inode->i_gid = raw_inode->i_gid | ((uint32_t)raw_inode->i_gid_high << 16);
    inode->i_nlink = raw_inode->i_links_count;
    
    /* 填充 Ext4 私有数据 */
    memcpy(ei->i_block, raw_inode->i_block, sizeof(raw_inode->i_block));
    ei->i_flags = raw_inode->i_flags;
    
    /* 设置 inode 操作和默认文件操作（参考 Linux fs/ext4/inode.c） */
    if (S_ISDIR(inode->i_mode)) {
        inode->i_op = &ext4_dir_inode_operations;
        inode->i_fop = &ext4_dir_operations;
    } else {
        inode->i_op = &ext4_file_inode_operations;
        inode->i_fop = &ext4_file_operations;
    }
    
    free(buf);
    
    return inode;
}

/**
 * ext4_write_inode - 将 VFS inode 写回 Ext4 磁盘 inode
 *
 * 参考 Linux 内核 fs/ext4/inode.c::ext4_write_inode（极简版，无日志、无事务）
 */
static int ext4_write_inode(struct inode *inode, struct writeback_control *wbc)
{
    struct super_block *sb = inode->i_sb;
    struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
    struct ext4_inode_info *ei;
    struct ext4_inode *raw_inode;
    uint32_t block_size;
    uint32_t inodes_per_group;
    uint32_t group, index;
    uint32_t inode_table_block;
    uint32_t inode_offset;
    uint32_t inode_size = 256; /* 与 ext4_mkfs / ext4_iget 保持一致 */
    char *buf;
    int ret;
    uint64_t size;
    uint64_t blocks;

    (void)wbc; /* 当前未使用 */

    if (!sbi || !sbi->s_group_desc) {
        return -1;
    }

    block_size = sbi->s_block_size;
    inodes_per_group = sbi->s_inodes_per_group;

    if (inode->i_ino == 0) {
        return -1;
    }

    group  = (inode->i_ino - 1) / inodes_per_group;
    index  = (inode->i_ino - 1) % inodes_per_group;

    /* 简化版：仅支持单块组文件系统 */
    if (group != 0) {
        return -1;
    }

    /* 计算 inode 表块号与偏移（与 ext4_iget 相同） */
    inode_table_block = sbi->s_group_desc->bg_inode_table_lo;
    inode_offset = index * inode_size;
    inode_table_block += inode_offset / block_size;
    inode_offset %= block_size;

    buf = (char *)malloc(block_size);
    if (!buf) {
        return -1;
    }

    /* 读取包含该 inode 的块 */
    ret = ext4_read_block(inode_table_block, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }

    raw_inode = (struct ext4_inode *)(buf + inode_offset);
    ei = (struct ext4_inode_info *)inode->i_private;
    if (!ei) {
        free(buf);
        return -1;
    }

    /* 将 VFS inode 字段写回到磁盘 inode 结构 */
    raw_inode->i_mode = inode->i_mode;

    size = (uint64_t)inode->i_size;
    raw_inode->i_size_lo   = (uint32_t)(size & 0xFFFFFFFFULL);
    raw_inode->i_size_high = (uint32_t)(size >> 32);

    blocks = (uint64_t)inode->i_blocks;
    raw_inode->i_blocks_lo = (uint32_t)(blocks & 0xFFFFFFFFULL);
    raw_inode->i_blocks_hi = (uint16_t)((blocks >> 32) & 0xFFFFU);

    raw_inode->i_atime = (uint32_t)inode->i_atime;
    raw_inode->i_mtime = (uint32_t)inode->i_mtime;
    raw_inode->i_ctime = (uint32_t)inode->i_ctime;

    /* UID / GID 拆分为高低位 */
    raw_inode->i_uid      = (uint16_t)(inode->i_uid & 0xFFFFU);
    raw_inode->i_uid_high = (uint16_t)((inode->i_uid >> 16) & 0xFFFFU);
    raw_inode->i_gid      = (uint16_t)(inode->i_gid & 0xFFFFU);
    raw_inode->i_gid_high = (uint16_t)((inode->i_gid >> 16) & 0xFFFFU);

    raw_inode->i_links_count = (uint16_t)inode->i_nlink;

    /* Ext4 私有数据：块指针和标志 */
    memcpy(raw_inode->i_block, ei->i_block, sizeof(raw_inode->i_block));
    raw_inode->i_flags = ei->i_flags;

    /* 其余字段（ACL、OS 特定字段等）保持不变 */

    /* 将修改后的块写回磁盘 */
    ret = ext4_write_block(inode_table_block, buf);

    free(buf);

    if (ret < 0) {
        return -1;
    }

    return 0;
}

/**
 * ext4_mount - 挂载 Ext4 文件系统
 * 
 * 使用 VFS 的 mount_bdev 统一接口，ext4 只需要实现 fill_super
 */
static struct dentry *ext4_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data)
{
    /* 使用 VFS 的统一挂载接口 */
    return mount_bdev(fs_type, flags, dev_name, data, ext4_fill_super);
}

/**
 * ext4_kill_sb - 卸载文件系统
 */
static void ext4_kill_sb(struct super_block *sb)
{
    struct ext4_sb_info *sbi;
    
    if (!sb) {
        return;
    }
    
    sbi = (struct ext4_sb_info *)sb->s_fs_info;
    if (sbi) {
        if (sbi->s_group_desc) {
            free(sbi->s_group_desc);
        }
        free(sbi);
        sb->s_fs_info = NULL;
    }
}
