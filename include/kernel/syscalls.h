#ifndef __SYSCALL_H_
#define __SYSCALL_H_

#include <linux/types.h>
#include <kernel/interrupts.h>
#include <kernel/multitasking.h>

class SyscallHandler : public InterruptHandler
{
public:
    SyscallHandler(InterruptManager * interruptManager);
    ~SyscallHandler();

    uint32_t HandleInterrupt(uint32_t esp);
};

# endif