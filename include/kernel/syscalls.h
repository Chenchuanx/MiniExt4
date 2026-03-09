#ifndef __SYSCALL_H_
#define __SYSCALL_H_

#include <linux/types.h>
#include <kernel/interrupts.h>
#include <kernel/multitasking.h>

// 系统调用号定义（供内核和 Shell 共同使用）
static const uint32_t SYS_WRITE = 0;  // 核心：向控制台输出字符串
static const uint32_t SYS_TIME  = 1;  // 时间：显示当前时间
static const uint32_t SYS_PWD   = 2;  // 文件系统：显示当前工作目录
static const uint32_t SYS_OPEN  = 3;  // 文件系统：打开文件/目录
static const uint32_t SYS_MKDIR = 4;  // 文件系统：创建目录
static const uint32_t SYS_CHDIR = 5;  // 文件系统：改变当前工作目录
static const uint32_t SYS_CLOSE = 6;  // 文件系统：关闭文件/目录
static const uint32_t SYS_GETDENTS = 7; // 文件系统：获取目录项

class SyscallHandler : public InterruptHandler
{
public:
    SyscallHandler(InterruptManager * interruptManager);
    ~SyscallHandler();

    uint32_t HandleInterrupt(uint32_t esp);
};

# endif