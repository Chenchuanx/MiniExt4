#include <kernel/tasks.h>
#include <kernel/syscalls.h>

// 系统调用：打印字符串（外部声明）
extern void sysprintf(const int8_t * str);

// 示例任务A
void taskA()
{
    while(true)
    {
        sysprintf("A");
    }
}

// 示例任务B
void taskB()
{
    while(true)
    {
        sysprintf("B");
    }
}

