#ifndef __SYSCALL_H_
#define __SYSCALL_H_

#include <linux/types.h>
#include <kernel/interrupts.h>
#include <kernel/multitasking.h>

// 系统调用号定义（供内核和 Shell 共同使用）
static const uint32_t SYS_WRITE = 0;  // 核心：向控制台输出字符串
static const uint32_t SYS_TIME  = 1;  // 时间：显示当前时间
static const uint32_t SYS_OPEN  = 2;  // 文件系统：打开文件/目录
static const uint32_t SYS_MKDIR = 3;  // 文件系统：创建目录
static const uint32_t SYS_CHDIR = 4;  // 文件系统：改变当前工作目录
static const uint32_t SYS_CLOSE = 5;  // 文件系统：关闭文件/目录
static const uint32_t SYS_GETDENTS = 6; // 文件系统：获取目录项
static const uint32_t SYS_GETCWD  = 7;  // 文件系统：获取当前工作目录

class SyscallHandler : public InterruptHandler
{
public:
    SyscallHandler(InterruptManager * interruptManager);
    ~SyscallHandler();

    uint32_t HandleInterrupt(uint32_t esp);
};

# endif