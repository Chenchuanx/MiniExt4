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
    __asm__("int $0x80": : "a" (SYS_WRITE), "b" (str));
}

// 系统调用：显示时间
void systime()
{
    __asm__ ("int $0x80": : "a" (SYS_TIME));
}

// 系统调用：显示当前工作目录（pwd）
static void syspwd()
{
    __asm__ ("int $0x80": : "a" (SYS_PWD));
}

// 系统调用：列出当前目录内容（ls）
static void sysls()
{
    __asm__ ("int $0x80": : "a" (SYS_LS), "b" (0));
}

// 系统调用：创建目录（mkdir）
static void sysmkdir(const int8_t *path)
{
    __asm__ ("int $0x80": : "a" (SYS_MKDIR), "b" (path));
}

// 系统调用：改变当前工作目录（cd / chdir）
static void syschdir(const int8_t *path)
{
    __asm__ ("int $0x80": : "a" (SYS_CHDIR), "b" (path));
}

// 命令解析辅助：匹配关键字 + 可选参数
// 匹配成功返回 1，并在 *arg 中返回参数起始指针（无参数则为 0），否则返回 0
static int match_command_with_arg(const int8_t *cmd, const char *keyword, const int8_t **arg)
{
    int i = 0;

    if (!cmd || !keyword || !arg) {
        return 0;
    }

    // 逐字符匹配关键字
    while (keyword[i] != '\0' && cmd[i] != '\0' && cmd[i] == (int8_t)keyword[i]) {
        ++i;
    }

    // 关键字未完全匹配
    if (keyword[i] != '\0') {
        return 0;
    }

    // 关键字之后必须是结束符或空格，否则认为不是该命令（例如 "mkdirx"）
    if (cmd[i] != '\0' && cmd[i] != ' ') {
        return 0;
    }

    // 跳过空格，找到参数起点
    while (cmd[i] == ' ') {
        ++i;
    }

    *arg = (cmd[i] == '\0') ? 0 : &cmd[i];
    return 1;
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
        else if (strcmp(cmd, "pwd") == 0)
        {
            syspwd();
        }
        else if (strcmp(cmd, "ls") == 0)
        {
            // 简化版：只支持列出当前目录（当前实现为根目录）
            sysls();
        }
        else
        {
            const int8_t *arg = 0;

            // mkdir 命令：支持 "mkdir name" 或 "mkdir /name"
            if (match_command_with_arg(cmd, "mkdir", &arg))
            {
                if (!arg) {
                    sysprintf((int8_t*)"mkdir: missing operand\n");
                } else {
                    sysmkdir(arg);
                }
            }
            // cd 命令：支持 "cd /path" 或 "cd name"
            else if (match_command_with_arg(cmd, "cd", &arg))
            {
                if (!arg) {
                    sysprintf((int8_t*)"cd: missing operand\n");
                } else {
                    syschdir(arg);
                }
            }
            else if (cmd[0] != '\0')
            {
                sysprintf("unknow command\n");
            }
        }
        sysprintf("ChenYingXing:>");
    }
}

