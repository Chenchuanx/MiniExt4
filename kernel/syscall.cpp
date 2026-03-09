# include <kernel/syscalls.h>
# include <lib/printf.h>
# include <drivers/rtc.h>
# include <linux/fs.h>

static int sys_ls_filldir(void *ctx, const char *name, int name_len,
                          loff_t off, u64 ino, unsigned type)
{
    (void)ctx;
    (void)off;
    (void)ino;
    (void)type;

    /* 跳过 "." 和 ".." */
    if ((name_len == 1 && name[0] == '.') ||
        (name_len == 2 && name[0] == '.' && name[1] == '.')) {
        return 0;
    }

    int8_t buf[256];
    int len = name_len < 255 ? name_len : 255;
    for (int i = 0; i < len; i++) {
        buf[i] = (int8_t)name[i];
    }
    buf[len] = '\0';

    printf(buf);
    printf((int8_t*)"\n");
    return 0;
}

static int sys_ls_do(const char *path)
{
    return vfs_ls(path, nullptr, (filldir_t)sys_ls_filldir);
}

SyscallHandler::SyscallHandler(InterruptManager * interruptManager)
    : InterruptHandler(0x80, interruptManager)
{

}

SyscallHandler::~SyscallHandler()
{

}

uint32_t SyscallHandler::HandleInterrupt(uint32_t esp)
{
    CPUState * cpu = (CPUState*)esp;

    switch(cpu->eax)
    {
    case SYS_WRITE:
        printf((int8_t*)cpu->ebx);
        break;
    case SYS_TIME:
    {
        rtc_time t{};
        rtc_read_time(&t);

        printfHex(t.hour);
        printf((int8_t*)":");
        printfHex(t.minute);
        printf((int8_t*)":");
        printfHex(t.second);
        printf((int8_t*)"\n");
        break;
    }
    case SYS_PWD:
    {
        char buf[256];
        int ret = vfs_getcwd(buf, sizeof(buf));
        if (ret == 0) {
            printf((int8_t*)buf);
            printf((int8_t*)"\n");
        } else {
            printf((int8_t*)"pwd: error\n");
        }
        break;
    }
    case SYS_CHDIR:
    {
        const char *path = (const char *)cpu->ebx;
        int ret = vfs_chdir(path);
        if (ret != 0) {
            printf((int8_t*)"cd: failed\n");
        }
        break;
    }
    case SYS_MKDIR:
    {
        const char *path = (const char *)cpu->ebx;
        int ret = vfs_mkdir(path, 0755);
        if (ret != 0) {
            printf((int8_t*)"mkdir: failed\n");
        }
        break;
    }
    case SYS_LS:
    {
        // 简化版：忽略寄存器参数，只列出当前目录（当前实现为根目录）
        int ret = sys_ls_do(nullptr);
        if (ret < 0) {
            printf((int8_t*)"ls: cannot access path\n");
        }
        break;
    }
    default:
        break;
    }

    return esp; 
}