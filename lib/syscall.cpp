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

// 系统调用：列出当前目录内容（ls）
void sysLs()
{
    __asm__("int $0x80" : : "a"(SYS_LS), "b"(0));
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

