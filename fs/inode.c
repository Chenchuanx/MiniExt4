/* 
 * VFS inode 管理
 * 参考 Linux 内核 fs/inode.c
 * 提供 inode 的分配、释放等基础功能
 */

#include <linux/fs.h>
#include <fs/inode.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* 
 * 简化版 inode 分配：使用静态池，不依赖 new/delete
 * 这在内核环境中更合理，也避免了对运行时库的依赖。
 */

#define VFS_MAX_INODES 128

static struct inode vfs_inode_pool[VFS_MAX_INODES];
static int vfs_inode_used[VFS_MAX_INODES];  /* 使用 int 而不是 bool，避免 C/C++ 兼容问题 */

/* 简化的内存操作函数 */
static void *simple_memset(void *s, int c, size_t n)
{
	unsigned char *p = (unsigned char *)s;
	size_t i;
	for (i = 0; i < n; i++) {
		p[i] = (unsigned char)c;
	}
	return s;
}

#define memset simple_memset

/**
 * vfs_alloc_inode - 分配一个 VFS inode
 * @sb: 超级块指针
 * 
 * 参考 Linux 内核 fs/inode.c
 * 返回分配的 inode 指针，失败返回 NULL
 */
struct inode *vfs_alloc_inode(struct super_block *sb)
{
	int i;
	struct inode *node;

	if (!sb) {
		return NULL;
	}

	/* 线性扫描静态池，找到一个空闲 inode */
	for (i = 0; i < VFS_MAX_INODES; i++) {
		if (!vfs_inode_used[i]) {
			node = &vfs_inode_pool[i];

			/* 标记已使用 */
			vfs_inode_used[i] = 1;

			/* 基础初始化（只初始化核心字段） */
			memset(node, 0, sizeof(*node));
			node->i_sb = sb;
			node->i_count = 1;
			node->i_state = I_NEW;
			node->i_flags = 0;
			node->i_size = 0;
			node->i_blocks = 0;
			node->i_ino = (unsigned long)(i + 1); /* 简单分配一个 inode 号 */
			
			INIT_LIST_HEAD(&node->i_list);
			INIT_LIST_HEAD(&node->i_sb_list);

			return node;
		}
	}

	/* 没有可用 inode */
	return NULL;
}

/**
 * vfs_free_inode - 释放一个 VFS inode
 * @node: inode 指针
 * 
 * 参考 Linux 内核 fs/inode.c
 */
void vfs_free_inode(struct inode *node)
{
	int i;

	if (!node) {
		return;
	}

	/* 在静态池中找到对应条目并标记为空闲 */
	for (i = 0; i < VFS_MAX_INODES; i++) {
		if (&vfs_inode_pool[i] == node) {
			vfs_inode_used[i] = 0;
			return;
		}
	}
}

