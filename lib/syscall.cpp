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

// 系统调用：显示当前工作目录（pwd）
void sysPwd()
{
    __asm__("int $0x80" : : "a"(SYS_PWD));
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

#include <linux/dirent.h>

// 辅助函数：计算字符串长度
static int str_len(const char* s) {
    int len = 0;
    while(s[len]) len++;
    return len;
}

// 系统调用：列出当前目录内容（ls）
void sysLs()
{
    int fd = sysOpen(".", 0, 0);
    if (fd < 0) {
        sysPrintf((int8_t*)"ls: cannot access path\n");
        return;
    }

    char buf[1024];
    int nread;

    while ((nread = sysGetdents(fd, buf, sizeof(buf))) > 0) {
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent *d = (struct linux_dirent *)(buf + bpos);
            
            // 跳过 "." 和 ".."
            int name_len = str_len(d->d_name);
            if (!((name_len == 1 && d->d_name[0] == '.') || 
                  (name_len == 2 && d->d_name[0] == '.' && d->d_name[1] == '.'))) {
                sysPrintf((int8_t*)d->d_name);
                sysPrintf((int8_t*)"\n");
            }

            bpos += d->d_reclen;
        }
    }

    sysClose(fd);
}

// 系统调用：创建目录（mkdir）
void sysMkdir(const int8_t *path)
{
    __asm__("int $0x80" : : "a"(SYS_MKDIR), "b"(path));
}

// 系统调用：改变当前工作目录（cd / chdir）
void sysChdir(const int8_t *path)
{
    __asm__("int $0x80" : : "a"(SYS_CHDIR), "b"(path));
}

