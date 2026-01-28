/* Ext4 超级块相关占位实现（简化版） */

#include <linux/fs.h>
#include <fs/fs_types.h>

/* 前向声明：Ext4 专用超级块初始化（后续可扩展） */
static struct dentry *ext4_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data);
static void ext4_kill_sb(struct super_block *sb);

/* Ext4 的 super_operations，占位实现 */
static const struct super_operations ext4_sops = {
    /* 目前全部使用默认空实现，后续可逐步填充 */
    /* alloc_inode   */ nullptr,
    /* destroy_inode */ nullptr,
    /* put_super     */ nullptr,
    /* sync_fs       */ nullptr,
    /* statfs        */ nullptr,
    /* remount_fs    */ nullptr,
    /* umount_begin  */ nullptr,
    /* dirty_inode   */ nullptr,
    /* write_inode   */ nullptr,
    /* evict_inode   */ nullptr,
};

/* Ext4 文件系统类型描述 */
struct file_system_type ext4_fs_type = {
    /* name   */ "ext4",
    /* fs_flags */ 0,
    /* mount */ ext4_mount,
    /* kill_sb */ ext4_kill_sb,
    /* fs_supers */ { nullptr, nullptr },
    /* owner */ nullptr,
    /* fs_supers_initialized */ false,
};

static struct dentry *ext4_mount(struct file_system_type *fs_type,
                                 int flags, const char *dev_name, void *data)
{
    (void)fs_type;
    (void)flags;
    (void)dev_name;
    (void)data;

    /* 占位实现：暂时不真正挂载，返回空指针 */
    return nullptr;
}

static void ext4_kill_sb(struct super_block *sb)
{
    (void)sb;
    /* 占位实现：暂时不做任何资源清理 */
}


