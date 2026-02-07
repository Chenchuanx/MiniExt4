/* Ext4 inode 相关占位实现（简化版） */

#include <linux/fs.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Ext4 普通文件 inode 操作，占位实现 */
const struct inode_operations ext4_file_inode_operations = {
    NULL,  /* create  */
    NULL,  /* lookup  */
    NULL,  /* link    */
    NULL,  /* unlink  */
    NULL,  /* symlink */
    NULL,  /* mkdir   */
    NULL,  /* rmdir   */
    NULL,  /* mknod   */
    NULL,  /* rename  */
    NULL,  /* getattr */
    NULL,  /* setattr */
    NULL,  /* get_link */
};

/* Ext4 目录 inode 操作，占位实现 */
const struct inode_operations ext4_dir_inode_operations = {
    NULL,  /* create  */
    NULL,  /* lookup  */
    NULL,  /* link    */
    NULL,  /* unlink  */
    NULL,  /* symlink */
    NULL,  /* mkdir   */
    NULL,  /* rmdir   */
    NULL,  /* mknod   */
    NULL,  /* rename  */
    NULL,  /* getattr */
    NULL,  /* setattr */
    NULL,  /* get_link */
};

