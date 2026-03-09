#ifndef __LIB_SYSCALL_H_
#define __LIB_SYSCALL_H_

#include <linux/types.h>

// 用户态系统调用封装接口
void sysPrintf(const int8_t *str);
void sysTime();
void sysPwd();
void sysLs();
void sysMkdir(const int8_t *path);
void sysChdir(const int8_t *path);

int sysOpen(const char *path, int flags, int mode);
int sysClose(int fd);
int sysGetdents(int fd, void *dirent, unsigned int count);

#endif

