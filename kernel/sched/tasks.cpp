#include <kernel/tasks.h>
#include <kernel/syscalls.h>
#include <lib/syscall.h>

// 示例任务A
void taskA()
{
    while(true)
    {
        sysPrintf("A");
    }
}

// 示例任务B
void taskB()
{
    while(true)
    {
        sysPrintf("B");
    }
}

