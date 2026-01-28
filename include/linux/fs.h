/* SPDX-License-Identifier: GPL-2.0 */
/* 
 * VFS 核心定义
 * 按照 Linux 内核标准组织
 */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/types.h>
#include <linux/list.h>

/* 前向声明 */
struct super_block;
struct inode;
struct dentry;
struct file;
struct file_system_type;
struct block_device;
struct address_space;

/* 包含操作函数定义 */
#include <fs/operations.h>

/* 包含结构体与类型定义 */
#include <fs/super.h>
#include <fs/fs_types.h>
#include <fs/inode.h>
#include <fs/dentry.h>
#include <fs/file.h>

/* VFS 公共接口（简化版） */

/* 文件系统注册 / 注销 */
int register_filesystem(struct file_system_type *fs);
int unregister_filesystem(struct file_system_type *fs);

/* inode 分配 / 释放 */
struct inode *vfs_alloc_inode(struct super_block *sb);
void vfs_free_inode(struct inode *node);

#endif /* _LINUX_FS_H */

