/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * Ext4 超级块和挂载实现
 */
#include <linux/fs.h>
#include <fs/fs_types.h>
#include <fs/ext4/ext4.h>
#include <lib/console.h>

/* 前向声明 */
extern int ext4_read_block(uint32_t blocknr, void *buf);
extern void ext4_set_block_size(uint32_t size);

/* 简化的内存操作函数（避免依赖标准库） */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	for (size_t i = 0; i < n; i++) {
		p[i] = (unsigned char)c;
	}
	return s;
}

static void *simple_memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = (unsigned char *)dest;
	const unsigned char *s = (const unsigned char *)src;
	for (size_t i = 0; i < n; i++) {
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
static bool malloc_used[MAX_MALLOC_BLOCKS];

static void *simple_malloc(size_t size)
{
	if (size > MAX_MALLOC_SIZE) {
		return nullptr;
	}
	for (int i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (!malloc_used[i]) {
			malloc_used[i] = true;
			return malloc_pool[i];
		}
	}
	return nullptr;
}

static void simple_free(void *ptr)
{
	if (!ptr) return;
	for (int i = 0; i < MAX_MALLOC_BLOCKS; i++) {
		if (malloc_pool[i] == ptr) {
			malloc_used[i] = false;
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
static struct inode *ext4_iget(struct super_block *sb, unsigned long ino);

/* Ext4 的 super_operations */
static const struct super_operations ext4_sops = {
    ext4_alloc_inode,      /* alloc_inode */
    ext4_destroy_inode,    /* destroy_inode */
    nullptr,               /* put_super */
    nullptr,               /* sync_fs */
    nullptr,               /* statfs */
    nullptr,               /* remount_fs */
    nullptr,               /* umount_begin */
    nullptr,               /* dirty_inode */
    nullptr,               /* write_inode */
    nullptr,               /* evict_inode */
};

/* Ext4 文件系统类型描述 */
struct file_system_type ext4_fs_type = {
    "ext4",
    0,
    ext4_mount,
    ext4_kill_sb,
    { nullptr, nullptr },
    nullptr,
    false,
    nullptr,
};

/* 简化的 dentry 分配（静态池） */
#define MAX_DENTRIES 64
static struct dentry dentry_pool[MAX_DENTRIES];
static bool dentry_used[MAX_DENTRIES];

static struct dentry *alloc_dentry(void)
{
    for (int i = 0; i < MAX_DENTRIES; i++) {
        if (!dentry_used[i]) {
            dentry_used[i] = true;
            struct dentry *d = &dentry_pool[i];
            memset(d, 0, sizeof(*d));
            d->d_count = 1;
            INIT_LIST_HEAD(&d->d_child);
            INIT_LIST_HEAD(&d->d_subdirs);
            return d;
        }
    }
    return nullptr;
}

/**
 * ext4_alloc_inode - 分配 Ext4 inode
 */
static struct inode *ext4_alloc_inode(struct super_block *sb)
{
    struct inode *inode = vfs_alloc_inode(sb);
    if (!inode) {
        return nullptr;
    }
    
    /* 分配 Ext4 私有数据 */
    struct ext4_inode_info *ei = (struct ext4_inode_info *)malloc(sizeof(*ei));
    if (!ei) {
        vfs_free_inode(inode);
        return nullptr;
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
        inode->i_private = nullptr;
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
    uint32_t inode_size = 128;  /* 默认 inode 大小 128 字节 */
    int ret;
    
    /* 设置块大小 */
    ext4_set_block_size(block_size);
    
    /* 计算文件系统参数 */
    blocks_per_group = 32768;  /* 简化：固定每组 32768 块 */
    inodes_per_group = blocks_per_group / 4;  /* 简化：每 4 块一个 inode */
    groups_count = (total_blocks + blocks_per_group - 1) / blocks_per_group;
    first_data_block = (block_size == 1024) ? 1 : 0;
    
    /* 分配缓冲区 */
    buf = (char *)malloc(block_size);
    if (!buf) {
        return -1;
    }
    memset(buf, 0, block_size);
    
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
    esb->s_free_blocks_count_lo = total_blocks - esb->s_r_blocks_count_lo - 10;  /* 减去系统块 */
    esb->s_free_inodes_count = esb->s_inodes_count - 10;  /* 减去系统 inode */
    esb->s_first_data_block = first_data_block;
    esb->s_log_block_size = (block_size == 1024) ? 0 : (block_size == 2048) ? 1 : 2;  /* 简化 */
    esb->s_log_cluster_size = 0;
    esb->s_blocks_per_group = blocks_per_group;
    esb->s_clusters_per_group = blocks_per_group;
    esb->s_inodes_per_group = inodes_per_group;
    esb->s_mtime = 0;  /* 简化：使用 0 */
    esb->s_wtime = 0;
    esb->s_mnt_count = 0;
    esb->s_max_mnt_count = 0xFFFF;  /* 无限制 */
    esb->s_magic = EXT4_SUPER_MAGIC;
    esb->s_state = 1;  /* 文件系统干净 */
    esb->s_errors = 1;  /* 继续 */
    esb->s_minor_rev_level = 0;
    esb->s_lastcheck = 0;
    esb->s_checkinterval = 0;
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
    
    /* 生成简单的 UUID（简化版） */
    for (int i = 0; i < 16; i++) {
        esb->s_uuid[i] = (uint8_t)(i * 17);
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
    gd->bg_free_blocks_count_lo = blocks_per_group - 10;  /* 减去系统块 */
    gd->bg_free_inodes_count_lo = inodes_per_group - 10;
    gd->bg_used_dirs_count_lo = 0;
    gd->bg_flags = 0;
    gd->bg_checksum = 0;  /* 简化：不计算校验和 */
    
    /* 写入 group descriptor */
    /* 对于 4KB 块：group descriptor 在块 1（first_data_block + 1 = 0 + 1 = 1）*/
    /* 对于 1KB 块：group descriptor 在块 2（因为 superblock 在块 1）*/
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
    for (uint32_t i = 0; i < group0_inode_table + inode_table_blocks; i++) {
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
    root_inode->i_size_lo = block_size;  /* 根目录至少一个块 */
    root_inode->i_atime = 0;
    root_inode->i_ctime = 0;
    root_inode->i_mtime = 0;
    root_inode->i_dtime = 0;
    root_inode->i_gid = 0;
    root_inode->i_links_count = 2;  /* . 和 .. */
    root_inode->i_blocks_lo = 1;  /* 一个块 */
    root_inode->i_flags = 0;
    root_inode->i_block[0] = group0_inode_table + inode_table_blocks;  /* 根目录数据块 */
    
    ret = ext4_write_block(group0_inode_table, buf);
    if (ret < 0) {
        free(buf);
        return -1;
    }
    
    /* 初始化根目录数据块（包含 . 和 .. 条目） */
    memset(buf, 0, block_size);
    struct ext4_dir_entry *de = (struct ext4_dir_entry *)buf;
    
    /* . 条目 */
    de->inode = 2;
    de->rec_len = 12;
    de->name_len = 1;
    de->name[0] = '.';
    
    /* .. 条目 */
    de = (struct ext4_dir_entry *)((char *)de + de->rec_len);
    de->inode = 2;
    de->rec_len = block_size - 12;  /* 剩余空间 */
    de->name_len = 2;
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
    sb->s_op = &ext4_sops;
    sb->s_flags = 0;
    sb->s_fs_info = sbi;
    
    /* 复制 UUID */
    memcpy(sb->s_uuid.b, esb->s_uuid, 16);
    
    return 0;
}

/**
 * ext4_iget - 获取 inode（从磁盘读取）
 */
static struct inode *ext4_iget(struct super_block *sb, unsigned long ino)
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
        return nullptr;
    }
    
    /* 计算 inode 表块号 */
    inode_table_block = sbi->s_group_desc->bg_inode_table_lo;
    /* 使用 superblock 中的 inode 大小（从 ext4_mkfs 中设置） */
    uint32_t inode_size = 128;  /* 简化：固定 128 字节 */
    inode_offset = index * inode_size;
    inode_table_block += inode_offset / block_size;
    inode_offset %= block_size;
    
    /* 分配缓冲区 */
    buf = (char *)malloc(block_size);
    if (!buf) {
        return nullptr;
    }
    
    /* 读取包含 inode 的块 */
    ret = ext4_read_block(inode_table_block, buf);
    if (ret < 0) {
        printf("Failed to read inode table\n");
        free(buf);
        return nullptr;
    }
    
    raw_inode = (struct ext4_inode *)(buf + inode_offset);
    
    /* 分配 VFS inode */
    inode = ext4_alloc_inode(sb);
    if (!inode) {
        free(buf);
        return nullptr;
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
    
    free(buf);
    
    return inode;
}

/**
 * ext4_mount - 挂载 Ext4 文件系统
 */
static struct dentry *ext4_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data)
{
    struct super_block *sb;
    struct inode *root_inode;
    struct dentry *root_dentry;
    int ret;
    
    (void)fs_type;
    (void)flags;
    (void)dev_name;
    
    /* 分配 super_block（简化版，使用静态分配） */
    static struct super_block ext4_sb;
    memset(&ext4_sb, 0, sizeof(ext4_sb));
    sb = &ext4_sb;
    
    INIT_LIST_HEAD(&sb->s_list);
    sb->s_dev = 0;  /* 简化版 */
    sb->s_count = 1;
    sb->s_active = 1;
    
    /* 填充超级块 */
    ret = ext4_fill_super(sb, data);
    if (ret < 0) {
        printf("ext4_fill_super failed\n");
        return nullptr;
    }
    
    /* 获取根 inode */
    struct ext4_sb_info *sbi = (struct ext4_sb_info *)sb->s_fs_info;
    if (!sbi) {
        printf("sbi is null\n");
        return nullptr;
    }
    root_inode = ext4_iget(sb, sbi->s_root_ino);
    if (!root_inode) {
        printf("ext4_iget failed for root inode\n");
        return nullptr;
    }
    
    /* 创建根 dentry */
    root_dentry = alloc_dentry();
    if (!root_dentry) {
        vfs_free_inode(root_inode);
        return nullptr;
    }
    
    root_dentry->d_sb = sb;
    root_dentry->d_inode = root_inode;
    root_dentry->d_parent = root_dentry;  /* 根目录的父目录是自己 */
    root_dentry->d_name.name = (const unsigned char *)"/";
    root_dentry->d_name.len = 1;
    root_dentry->d_name.hash = 0;
    
    sb->s_root = root_dentry;
    
    return root_dentry;
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
        sb->s_fs_info = nullptr;
    }
}
