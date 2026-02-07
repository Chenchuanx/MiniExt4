/* 
 * VFS 核心定义
 */
#ifndef _LINUX_FS_H
#define _LINUX_FS_H

#include <linux/types.h>
#include <linux/list.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* Dentry 管理（参考 Linux fs/dcache.c） */
struct dentry *d_alloc(struct dentry *parent, const struct qstr *name);
struct dentry *d_lookup(struct dentry *parent, const struct qstr *name);
struct dentry *dget(struct dentry *dentry);
void dput(struct dentry *dentry);
void d_add(struct dentry *dentry, struct inode *inode);
void d_instantiate(struct dentry *dentry, struct inode *inode);
u32 d_hash_string(const char *str, int len);
void qstr_init(struct qstr *qstr, const char *name, int len);

/* 超级块和挂载管理（参考 Linux fs/super.c） */
struct dentry *get_sb_bdev(struct file_system_type *fs_type,
			    int flags, const char *dev_name, void *data,
			    int (*fill_super)(struct super_block *, void *));
struct dentry *mount_bdev(struct file_system_type *fs_type,
			  int flags, const char *dev_name, void *data,
			  int (*fill_super)(struct super_block *, void *));
void deactivate_super(struct super_block *sb);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_FS_H */

