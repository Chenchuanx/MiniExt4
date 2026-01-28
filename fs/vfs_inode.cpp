/* VFS inode 通用管理（简化版） */

#include <linux/fs.h>

/* 
 * 简化版 inode 分配：使用静态池，不依赖 new/delete
 * 这在内核环境中更合理，也避免了对运行时库的依赖。
 */

#define VFS_MAX_INODES 128

static struct inode vfs_inode_pool[VFS_MAX_INODES];
static bool         vfs_inode_used[VFS_MAX_INODES];

struct inode *vfs_alloc_inode(struct super_block *sb)
{
	if (!sb) {
		return 0;
	}

	/* 线性扫描静态池，找到一个空闲 inode */
	for (int i = 0; i < VFS_MAX_INODES; ++i) {
		if (!vfs_inode_used[i]) {
			struct inode *node = &vfs_inode_pool[i];

			/* 标记已使用 */
			vfs_inode_used[i] = true;

			/* 基础初始化（只初始化核心字段） */
			node->i_sb     = sb;
			node->i_count  = 1;
			node->i_state  = I_NEW;
			node->i_flags  = 0;
			node->i_size   = 0;
			node->i_blocks = 0;
			node->i_ino    = (unsigned long)(i + 1); /* 简单分配一个 inode 号 */

			return node;
		}
	}

	/* 没有可用 inode */
	return 0;
}

void vfs_free_inode(struct inode *node)
{
	if (!node) {
		return;
	}

	/* 在静态池中找到对应条目并标记为空闲 */
	for (int i = 0; i < VFS_MAX_INODES; ++i) {
		if (&vfs_inode_pool[i] == node) {
			vfs_inode_used[i] = false;
			return;
		}
	}
}

