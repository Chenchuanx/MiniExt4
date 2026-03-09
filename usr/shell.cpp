#include <usr/shell.h>
#include <drivers/keyboard.h>
#include <lib/printf.h>
#include <lib/syscall.h>
#include <linux/dirent.h>
#include <linux/string.h>

// Shell 提示符常量
const int8_t SHELL_PROMPT[] = "ChenYingXing:>";
const uint8_t SHELL_PROMPT_LEN = sizeof(SHELL_PROMPT) - 1;

// --- 具体命令实现 ---
static void cmd_time(const int8_t *arg) { 
    sysTime(); 
}

static void cmd_pwd(const int8_t *arg) { 
    char buf[256];
    int ret = sysGetcwd(buf, sizeof(buf));
    if (ret == 0) {
        sysPrintf((int8_t*)buf);
        sysPrintf((int8_t*)"\n");
    } else {
        sysPrintf((int8_t*)"pwd: error\n");
    }
}

static void cmd_ls(const int8_t *arg) { 
    const char *path = arg ? (const char *)arg : ".";
    int fd = sysOpen(path, 0, 0);
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
            
            int name_len = 0;
            while (d->d_name[name_len]) name_len++;

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

static void cmd_mkdir(const int8_t *arg) {
    if (!arg) {
        sysPrintf((int8_t*)"mkdir: missing operand\n");
    } else {
        int ret = sysMkdir(arg);
        if (ret != 0) {
            sysPrintf((int8_t*)"mkdir: failed\n");
        }
    }
}

static void cmd_cd(const int8_t *arg) {
    if (!arg) {
        sysPrintf((int8_t*)"cd: missing operand\n");
    } else {
        int ret = sysChdir(arg);
        if (ret != 0) {
            sysPrintf((int8_t*)"cd: failed\n");
        }
    }
}


// 定义命令处理函数类型
typedef void (*shell_cmd_handler_t)(const int8_t *arg);

struct shell_command {
    const char *name;
    shell_cmd_handler_t handler;
};

// 命令表
static struct shell_command cmd_table[] = {
    {"time", cmd_time},
    {"pwd", cmd_pwd},
    {"ls", cmd_ls},
    {"mkdir", cmd_mkdir},
    {"cd", cmd_cd},
    {0, 0}
};

// 提取命令名和参数
// 将 cmd 拆分为命令名和参数，并返回指向命令名和参数的指针
static void parse_command(int8_t *cmd, int8_t **name, const int8_t **arg)
{
    // 跳过前导空格
    while (*cmd == ' ') {
        cmd++;
    }

    if (*cmd == '\0') {
        *name = 0;
        *arg = 0;
        return;
    }

    *name = cmd;

    // 找到命令名的结束位置
    while (*cmd != '\0' && *cmd != ' ') {
        cmd++;
    }

    if (*cmd == '\0') {
        *arg = 0;
        return;
    }

    // 将命令名与参数用 '\0' 截断
    *cmd = '\0';
    cmd++;

    // 跳过参数前面的空格
    while (*cmd == ' ') {
        cmd++;
    }

    if (*cmd == '\0') {
        *arg = 0;
    } else {
        *arg = cmd;
    }
}

// 简单Shell命令处理
void simpleShell(const char c, KeyboardDriver * pKeyDriver)
{
    if(c == '\n')
    {
        int8_t cmd[256] = {0};
        pKeyDriver->get_buffer(cmd);

        int8_t *cmd_name = 0;
        const int8_t *cmd_arg = 0;

        parse_command(cmd, &cmd_name, &cmd_arg);

        if (cmd_name != 0) {
            bool found = false;
            for (int i = 0; cmd_table[i].name != 0; i++) {
                if (strcmp(cmd_name, cmd_table[i].name) == 0) {
                    cmd_table[i].handler(cmd_arg);
                    found = true;
                    break;
                }
            }

            if (!found) {
                sysPrintf((int8_t*)"unknown command: ");
                sysPrintf(cmd_name);
                sysPrintf((int8_t*)"\n");
            }
        }

        sysPrintf(SHELL_PROMPT);
    }
}

