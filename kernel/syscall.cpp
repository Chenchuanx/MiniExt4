# include <kernel/syscalls.h>
# include <lib/printf.h>
# include <drivers/rtc.h>
# include <linux/fs.h>

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
    case SYS_OPEN:
    {
        const char *path = (const char *)cpu->ebx;
        int flags = (int)cpu->ecx;
        int mode = (int)cpu->edx;
        cpu->eax = vfs_open(path, flags, mode);
        break;
    }
    case SYS_CLOSE:
    {
        int fd = (int)cpu->ebx;
        cpu->eax = vfs_close(fd);
        break;
    }
    case SYS_GETDENTS:
    {
        int fd = (int)cpu->ebx;
        char *dirent = (char *)cpu->ecx;
        unsigned int count = (unsigned int)cpu->edx;

        cpu->eax = vfs_getdents(fd, dirent, count);
        break;
    }
    default:
        break;
    }

    return esp; 
}