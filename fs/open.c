#include <linux/fs.h>
#include <linux/dirent.h>

#define MAX_FD 256
static struct file global_fd_table[MAX_FD];
static int fd_used[MAX_FD] = {0};

extern struct dentry *vfs_get_cwd_dentry(void);

int vfs_open(const char *path, int flags, int mode)
{
    int fd = -1;
    for (int i = 0; i < MAX_FD; i++) {
        if (!fd_used[i]) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return -1; // too many open files

    struct dentry *cwd = vfs_get_cwd_dentry();
    struct super_block *sb = vfs_get_root_sb();
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
        return -2; // not found
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
