/* Ext4 inode 相关占位实现（简化版） */

#include <linux/fs.h>

/* Ext4 普通文件 inode 操作，占位实现 */
const struct inode_operations ext4_file_inode_operations = {
    /* create  */ nullptr,
    /* lookup  */ nullptr,
    /* link    */ nullptr,
    /* unlink  */ nullptr,
    /* symlink */ nullptr,
    /* mkdir   */ nullptr,
    /* rmdir   */ nullptr,
    /* mknod   */ nullptr,
    /* rename  */ nullptr,
    /* getattr */ nullptr,
    /* setattr */ nullptr,
    /* get_link */ nullptr,
};

/* Ext4 目录 inode 操作，占位实现 */
const struct inode_operations ext4_dir_inode_operations = {
    /* create  */ nullptr,
    /* lookup  */ nullptr,
    /* link    */ nullptr,
    /* unlink  */ nullptr,
    /* symlink */ nullptr,
    /* mkdir   */ nullptr,
    /* rmdir   */ nullptr,
    /* mknod   */ nullptr,
    /* rename  */ nullptr,
    /* getattr */ nullptr,
    /* setattr */ nullptr,
    /* get_link */ nullptr,
};


