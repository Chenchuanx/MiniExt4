#ifndef __LIB_SYSCALL_H_
#define __LIB_SYSCALL_H_

#include <linux/types.h>

/* 系统调用 */
void sysPrintf(const int8_t *str);
void sysTime();

/* 文件系统 */
int sysOpen(const char *path, int flags, int mode);
int sysClose(int fd);
int sysGetdents(int fd, void *dirent, unsigned int count);
int sysGetcwd(char *buf, int size);
int sysMkdir(const int8_t *path);
int sysChdir(const int8_t *path);
int sysFileWrite(int fd, const char *buf, int count);
int sysFileRead(int fd, char *buf, int count);

#endif

