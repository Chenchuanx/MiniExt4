#include <lib/syscall.h>
#include <kernel/syscalls.h>

// 系统调用：打印字符串
void sysPrintf(const int8_t *str)
{
    __asm__("int $0x80" : : "a"(SYS_WRITE), "b"(str));
}

// 系统调用：显示时间
void sysTime()
{
    __asm__("int $0x80" : : "a"(SYS_TIME));
}

// 系统调用：获取当前工作目录
int sysGetcwd(char *buf, int size)
{
    int ret;
    __asm__("int $0x80" : "=a"(ret) : "a"(SYS_GETCWD), "b"(buf), "c"(size));
    return ret;
}

// 系统调用：打开文件/目录
int sysOpen(const char *path, int flags, int mode)
{
    int ret;
    __asm__("int $0x80" : "=a"(ret) : "a"(SYS_OPEN), "b"(path), "c"(flags), "d"(mode));
    return ret;
}

// 系统调用：关闭文件/目录
int sysClose(int fd)
{
    int ret;
    __asm__("int $0x80" : "=a"(ret) : "a"(SYS_CLOSE), "b"(fd));
    return ret;
}

// 系统调用：获取目录项
int sysGetdents(int fd, void *dirent, unsigned int count)
{
    int ret;
    __asm__("int $0x80" : "=a"(ret) : "a"(SYS_GETDENTS), "b"(fd), "c"(dirent), "d"(count));
    return ret;
}

// 系统调用：创建目录（mkdir）
int sysMkdir(const int8_t *path)
{
    int ret;
    __asm__("int $0x80" : "=a"(ret) : "a"(SYS_MKDIR), "b"(path));
    return ret;
}

// 系统调用：改变当前工作目录（cd / chdir）
int sysChdir(const int8_t *path)
{
    int ret;
    __asm__("int $0x80" : "=a"(ret) : "a"(SYS_CHDIR), "b"(path));
    return ret;
}

// 系统调用：向文件写入数据
int sysFileWrite(int fd, const char *buf, int count)
{
    int ret;
    __asm__("int $0x80" : "=a"(ret) : "a"(SYS_FWRITE), "b"(fd), "c"(buf), "d"(count));
    return ret;
}

