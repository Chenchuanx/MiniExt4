/*
 * 与 Linux asm-generic/errno-base.h 对齐的正值 errno。
 * VFS / 系统调用约定：失败时返回 -errno（例如 -ENOENT、-EINVAL）。
 */
#ifndef _LINUX_ERRNO_H
#define _LINUX_ERRNO_H

#define EPERM           1   /* 操作不被允许（权限不足） */
#define ENOENT          2   /* 文件或目录不存在 */
#define ESRCH           3   /* 进程不存在 */
#define EINTR           4   /* 系统调用被中断 */
#define EIO             5   /* I/O 错误 */
#define ENXIO           6   /* 设备或地址不存在 */
#define E2BIG           7   /* 参数列表过长 */
#define ENOEXEC         8   /* 可执行文件格式错误 */
#define EBADF           9   /* 非法文件描述符 */
#define ECHILD          10  /* 没有子进程 */
#define EAGAIN          11  /* 资源暂不可用，请重试 */
#define ENOMEM          12  /* 内存不足 */
#define EACCES          13  /* 权限拒绝 */
#define EFAULT          14  /* 非法地址 */
#define ENOTBLK         15  /* 不是块设备 */
#define EBUSY           16  /* 设备或资源忙 */
#define EEXIST          17  /* 文件已存在 */
#define EXDEV           18  /* 跨设备链接 */
#define ENODEV          19  /* 设备不存在 */
#define ENOTDIR         20  /* 不是目录 */
#define EISDIR          21  /* 是目录 */
#define EINVAL          22  /* 无效参数 */
#define ENFILE          23  /* 系统级文件表已满 */
#define EMFILE          24  /* 进程打开文件数已达上限 */
#define ENOTTY          25  /* 不适用于该设备的控制操作 */
#define ETXTBSY         26  /* 文本文件忙 */
#define EFBIG           27  /* 文件过大 */
#define ENOSPC          28  /* 设备空间不足 */
#define ESPIPE          29  /* 非法 seek 操作 */
#define EROFS           30  /* 只读文件系统 */
#define EMLINK          31  /* 硬链接数过多 */
#define EPIPE           32  /* 管道破裂（对端关闭） */
#define EDOM            33  /* 数学参数超出定义域 */
#define ERANGE          34  /* 数值结果超出范围 */
#define ENOTEMPTY       39  /* 目录非空 */

#endif /* _LINUX_ERRNO_H */
