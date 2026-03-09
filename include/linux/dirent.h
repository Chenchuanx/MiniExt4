#ifndef _LINUX_DIRENT_H
#define _LINUX_DIRENT_H

#include <linux/types.h>

struct linux_dirent {
    unsigned long  d_ino;     /* Inode number */
    unsigned long  d_off;     /* Offset to next linux_dirent */
    unsigned short d_reclen;  /* Length of this linux_dirent */
    char           d_name[];  /* Filename (null-terminated) */
};

#endif
