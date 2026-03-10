#include <linux/fs.h>
#include <linux/dirent.h>

#define MAX_FD 256
static struct file global_fd_table[MAX_FD];
static int fd_used[MAX_FD] = {0};

extern struct dentry *vfs_get_cwd_dentry(void);

int vfs_open(const char *path, int flags, int mode)
{
    struct super_block *sb;
    struct dentry *root;
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_used[i]) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return -1; // too many open files

    struct dentry *cwd = vfs_get_cwd_dentry();
    sb = vfs_get_root_sb();
    if (!sb || !sb->s_root) {
        return -3;
    }
    root = sb->s_root;
    struct dentry *target = 0;

    if (!path || path[0] == '\0') {
        target = cwd;
    } else if (path[0] == '/') {
        if (path[1] == '\0') {
            target = sb->s_root;
        } else {
            target = vfs_lookup_root(sb, path);
        }
    } else {
        target = vfs_path_lookup(cwd, path);
    }

    if (!target || !target->d_inode) {
        /* 如果指定了 O_CREAT，则尝试创建普通文件 */
        if (flags & O_CREAT) {
            const char *rel;
            const char *scan;
            const char *last_sep;
            const char *name_start;
            char name_buf[256];
            int name_len = 0;
            char parent_buf[256];
            int parent_len = 0;
            struct dentry *parent;

            if (!path || path[0] == '\0') {
                return -2;
            }

            /* 去掉前导 '/'，得到相对路径部分（逻辑同 vfs_mkdir） */
            rel = path;
            if (*rel == '/') {
                do {
                    rel++;
                } while (*rel == '/');
            }
            if (*rel == '\0') {
                return -2;
            }

            /* 查找最后一个 '/'，将路径拆分为 parent/path 和 name */
            last_sep = 0;
            scan = rel;
            while (*scan != '\0') {
                if (*scan == '/') {
                    last_sep = scan;
                }
                scan++;
            }

            if (!last_sep) {
                /* 没有 '/'：父目录就是起始目录（根或当前工作目录） */
                parent = (path[0] == '/') ? root : cwd;
                name_start = rel;
            } else {
                /* 有 '/'：前半部分是父路径，最后一个分量是新文件名 */
                parent_len = (int)(last_sep - rel);
                if (parent_len <= 0 || parent_len >= (int)sizeof(parent_buf)) {
                    return -2;
                }
                for (name_len = 0; name_len < parent_len; name_len++) {
                    parent_buf[name_len] = rel[name_len];
                }
                parent_buf[parent_len] = '\0';

                parent = (path[0] == '/') ? vfs_path_lookup(root, parent_buf)
                                          : vfs_path_lookup(cwd, parent_buf);
                if (!parent || !parent->d_inode) {
                    return -2;
                }
                if (!S_ISDIR(parent->d_inode->i_mode)) {
                    return -2;
                }

                name_start = last_sep + 1;
                if (*name_start == '\0') {
                    return -2;
                }
            }

            /* 拷贝名称分量，名称中不应再包含 '/' */
            name_len = 0;
            while (name_start[name_len] != '\0') {
                if (name_start[name_len] == '/') {
                    return -2;
                }
                if (name_len < 255) {
                    name_buf[name_len] = name_start[name_len];
                }
                name_len++;
            }
            name_buf[name_len < 255 ? name_len : 255] = '\0';

            /* 目录 inode 必须支持 create 操作 */
            if (!parent->d_inode || !parent->d_inode->i_op || !parent->d_inode->i_op->create) {
                return -2;
            }

            /* 构造 qstr 和 dentry，调用底层文件系统的 create */
            {
                struct qstr q;
                struct dentry *dentry;

                qstr_init(&q, name_buf, name_len);
                dentry = d_alloc(parent, &q);
                if (!dentry) {
                    return -2;
                }

                if (parent->d_inode->i_op->create(parent->d_inode, dentry, (umode_t)mode, (flags & O_EXCL) ? 1 : 0) != 0) {
                    return -2;
                }

                target = dentry;
            }
        } else {
            return -2; // not found
        }
    }

    struct inode *inode = target->d_inode;
    const struct file_operations *fops = inode->i_fop;

    struct file *f = &global_fd_table[fd];
    f->f_inode = inode;
    f->f_path_dentry = target;
    f->f_op = fops;
    f->f_pos = 0;
    f->f_flags = flags;
    f->f_mode = mode;
    f->f_count = 1;
    f->f_private = 0;

    if (fops && fops->open) {
        int err = fops->open(inode, f);
        if (err < 0) {
            return err;
        }
    }

    fd_used[fd] = 1;
    return fd;
}

int vfs_close(int fd)
{
    if (fd < 0 || fd >= MAX_FD || !fd_used[fd]) {
        return -1;
    }

    struct file *f = &global_fd_table[fd];
    if (f->f_op && f->f_op->release) {
        f->f_op->release(f->f_inode, f);
    }

    fd_used[fd] = 0;
    return 0;
}

struct getdents_context {
    char *buf;
    unsigned int count;
    unsigned int written;
    int error;
};

static int filldir_getdents(void *ctx, const char *name, int name_len,
                          loff_t off, u64 ino, unsigned type)
{
    struct getdents_context *p = (struct getdents_context *)ctx;
    
    int reclen = sizeof(struct linux_dirent) + name_len + 1;
    // 确保对齐
    reclen = (reclen + 3) & ~3;

    if (p->written + reclen > p->count) {
        if (p->written == 0) p->error = -1; // buffer too small
        return -1; // stop filling
    }

    struct linux_dirent *d = (struct linux_dirent *)(p->buf + p->written);
    d->d_ino = ino;
    d->d_off = off;
    d->d_reclen = reclen;
    
    for (int i = 0; i < name_len; i++) {
        d->d_name[i] = name[i];
    }
    d->d_name[name_len] = '\0';

    p->written += reclen;
    return 0;
}

int vfs_getdents(int fd, char *dirent, unsigned int count)
{
    if (fd < 0 || fd >= MAX_FD || !fd_used[fd]) {
        return -1;
    }

    struct file *f = &global_fd_table[fd];
    if (!f->f_op || !f->f_op->readdir) {
        return -2; // not a directory
    }

    struct getdents_context ctx;
    ctx.buf = dirent;
    ctx.count = count;
    ctx.written = 0;
    ctx.error = 0;

    int ret = f->f_op->readdir(f, &ctx, (filldir_t)filldir_getdents);
    if (ctx.error < 0) {
        return ctx.error;
    } else if (ret < 0 && ctx.written == 0) {
        return ret;
    }

    return ctx.written;
}
