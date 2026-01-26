#include <lib/shell.h>
#include <drivers/keyboard.h>
#include <kernel/syscalls.h>
#include <lib/console.h>

// 字符串比较函数
int8_t strcmp(const int8_t * src, const int8_t * dest)
{
    while((*src != '\0') && (*src == *dest))
    {
        ++src;
        ++dest;
    }
    return *src - *dest;
}

// 系统调用：打印字符串
void sysprintf(const int8_t * str)
{
    __asm__("int $0x80": : "a" (4), "b" (str));  // 软中断
}

// 系统调用：显示时间
void systime()
{
    __asm__ ("int $0x80": : "a" (1));
}

// 简单Shell命令处理
void simpleShell(const char c, KeyboardDriver * pKeyDriver)
{
    if(c == '\n')
    {
        int8_t cmd[256] = {0};
        pKeyDriver->get_buffer(cmd);

        if (strcmp(cmd, "time") == 0)
        {
            systime();
        }
        else if (cmd[0] != '\0')
        {
            sysprintf("unknow command\n");
        }
        sysprintf("ChenYingXing:>");
    }
}

