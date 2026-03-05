/* 
 * VFS 超级块和挂载管理
 * 参考 Linux 内核 fs/super.c
 * 提供统一的挂载接口，让文件系统只实现 fill_super
 */

#include <linux/fs.h>
#include <fs/fs_types.h>
#include <lib/console.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 简化的内存操作函数 */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	for (size_t i = 0; i < n; i++) {
		p[i] = (unsigned char)c;
	}
	return s;
}

#define memset simple_memset

/* 超级块链表头 */
static struct list_head super_blocks = { &super_blocks, &super_blocks };

/* 当前根超级块（简化：只挂载一个文件系统） */
static struct super_block *vfs_root_sb = (struct super_block *)NULL;

/* 文件系统类型链表（简化版全局链表） */
static struct file_system_type *g_filesystems = (struct file_system_type *)NULL;

/**
 * get_sb_bdev - 从块设备获取超级块（简化版）
 * @fs_type: 文件系统类型
 * @flags: 挂载标志
 * @dev_name: 设备名称（简化版暂不使用）
 * @data: 传递给 fill_super 的数据
 * @fill_super: 文件系统特定的 fill_super 函数
 * 
 * 返回挂载的根 dentry
 */
struct dentry *get_sb_bdev(struct file_system_type *fs_type,
			   int flags, const char *dev_name, void *data,
			   int (*fill_super)(struct super_block *, void *))
{
	struct super_block *sb;
	struct inode *root_inode;
	struct dentry *root_dentry;
	int ret;
	
	if (!fs_type || !fill_super) {
		return NULL;
	}
	
	/* 分配 super_block（简化版，使用静态分配） */
	static struct super_block sb_static;
	memset(&sb_static, 0, sizeof(sb_static));
	sb = &sb_static;
	
	/* 初始化超级块 */
	INIT_LIST_HEAD(&sb->s_list);
	sb->s_dev = 0;  /* 简化版，不处理设备号 */
	sb->s_count = 1;
	sb->s_active = 1;
	sb->s_type = fs_type;
	sb->s_flags = flags;
	
	/* 调用文件系统的 fill_super */
	ret = fill_super(sb, data);
	if (ret < 0) {
		printf("fill_super failed\n");
		return NULL;
	}
	
	/* 设置操作函数表 */
	if (fs_type->fs_flags & FS_REQUIRES_DEV) {
		/* 需要设备的文件系统，操作函数表应该在 fill_super 中设置 */
	}
	
	/* 添加到超级块链表 */
	list_add(&sb->s_list, &super_blocks);
	
	/* 记录根超级块（当前仅支持单一挂载） */
	if (vfs_root_sb == NULL) {
		vfs_root_sb = sb;
	}
	
	/* 获取根 inode（通过超级块操作） */
	if (!sb->s_op || !sb->s_op->alloc_inode) {
		printf("super_operations not set\n");
		return NULL;
	}
	
	/* 简化版：假设根 inode 号存储在 s_fs_info 中 */
	/* 实际应该由文件系统在 fill_super 中设置 s_root_ino 或类似字段 */
	/* 这里需要文件系统提供获取根 inode 的方法 */
	
	/* 临时方案：文件系统应该在 fill_super 中创建根 dentry 并设置到 sb->s_root */
	if (sb->s_root) {
		return sb->s_root;
	}
	
	printf("root dentry not set by fill_super\n");
	return NULL;
}

/**
 * vfs_get_root_sb - 获取当前根超级块
 * 
 * 简化版：返回第一个挂载的超级块
 */
struct super_block *vfs_get_root_sb(void)
{
	if (vfs_root_sb) {
		return vfs_root_sb;
	}
	
	if (!list_empty(&super_blocks)) {
		struct super_block *sb = list_entry(super_blocks.next, struct super_block, s_list);
		vfs_root_sb = sb;
		return sb;
	}
	
	return (struct super_block *)NULL;
}

/**
 * mount_bdev - 挂载块设备文件系统
 * @fs_type: 文件系统类型
 * @flags: 挂载标志
 * @dev_name: 设备名称
 * @data: 传递给 fill_super 的数据
 * @fill_super: 文件系统特定的 fill_super 函数
 * 
 * 这是文件系统 mount 函数的通用实现
 */
struct dentry *mount_bdev(struct file_system_type *fs_type,
			  int flags, const char *dev_name, void *data,
			  int (*fill_super)(struct super_block *, void *))
{
	return get_sb_bdev(fs_type, flags, dev_name, data, fill_super);
}

/**
 * deactivate_super - 停用超级块
 * @sb: 超级块指针
 */
void deactivate_super(struct super_block *sb)
{
	if (!sb) {
		return;
	}
	
	/* 从链表中移除 */
	if (!list_empty(&sb->s_list)) {
		list_del(&sb->s_list);
	}
	
	/* 调用文件系统的 kill_sb（如果存在） */
	if (sb->s_type && sb->s_type->kill_sb) {
		sb->s_type->kill_sb(sb);
	}
}

/**
 * register_filesystem - 注册文件系统类型
 * @fs: 文件系统类型指针
 * 
 * 参考 Linux 内核 fs/super.c
 */
int register_filesystem(struct file_system_type *fs)
{
	if (!fs) {
		return -1;
	}

	/* 初始化 fs_supers 链表 */
	INIT_LIST_HEAD(&fs->fs_supers);
	fs->fs_supers_initialized = 1;
	fs->owner = (struct module *)NULL; /* 简化：不处理模块引用计数 */

	/* 头插到全局链表 */
	fs->next = g_filesystems;
	g_filesystems = fs;

	return 0;
}

/**
 * unregister_filesystem - 注销文件系统类型
 * @fs: 文件系统类型指针
 * 
 * 参考 Linux 内核 fs/super.c
 */
int unregister_filesystem(struct file_system_type *fs)
{
	struct file_system_type **p;

	if (!fs) {
		return -1;
	}

	/* 从链表中移除 */
	p = &g_filesystems;
	while (*p) {
		if (*p == fs) {
			*p = fs->next;
			fs->fs_supers_initialized = 0;
			return 0;
		}
		p = &((*p)->next);
	}

	return -1;
}

