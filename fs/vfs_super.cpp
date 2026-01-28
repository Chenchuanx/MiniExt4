/* VFS super_block 通用管理（简化版） */

#include <linux/fs.h>
#include <fs/fs_types.h>

/* 简化版全局文件系统类型链表（单链表） */
static file_system_type *g_filesystems = nullptr;

int register_filesystem(struct file_system_type *fs)
{
    if (!fs) {
        return -1;
    }

    /* 头插到全局链表 */
    fs->fs_supers.next = nullptr;
    fs->fs_supers.prev = nullptr;

    fs->fs_supers_initialized = true;

    fs->owner = nullptr; /* 简化：不处理模块引用计数 */

    fs->next = g_filesystems;
    g_filesystems = fs;

    return 0;
}

int unregister_filesystem(struct file_system_type *fs)
{
    if (!fs) {
        return -1;
    }

    file_system_type **p = &g_filesystems;
    while (*p) {
        if (*p == fs) {
            *p = nullptr; /* 简化：当前项目只注册一个文件系统 */
            fs->fs_supers_initialized = false;
            return 0;
        }
        p = &((*p)->next);
    }

    return -1;
}


