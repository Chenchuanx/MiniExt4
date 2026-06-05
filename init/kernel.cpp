#include <linux/types.h>
#include <mm/gdt.h>
#include <kernel/interrupts.h>
#include <drivers/keyboard.h>
#include <drivers/driver.h>
#include <drivers/mouse.h>
#include <kernel/syscalls.h>
#include <drivers/video/fb_console.h>
#include <lib/printf.h>
#include <kernel/shell.h>
#include <drivers/handlers.h>
#include <linux/ata.h>
#include <drivers/pit.h>
#include <linux/fs.h>
#include <fs/ext4/ext4.h>

// 构造函数类型定义
using constructor = void (*)();
extern constructor start_ctors;
extern constructor end_ctors;

// 调用所有全局对象的构造函数
extern "C" void callConstructors()
{
    for(constructor * i = &start_ctors; i != &end_ctors; ++i)
        (*i)();
}

void display_banner() {
    printf("\t\t  _______     ____   ______   _____ \n");
    printf("\t\t  / ____\\ \\   / /\\ \\ / / __ \\ / ____|\n");
    printf("\t\t | |     \\ \\_/ /  \\ V / |  | | (___  \n");
    printf("\t\t | |      \\   /    > <| |  | |\\___ \\ \n");
    printf("\t\t | |____   | |    / . \\ |__| |____) |\n");
    printf("\t\t  \\_____|  |_|   /_/ \\_\\____/|_____/ \n");
    printf("\t\t                                     \n");
    printf("\t\t                                     \n");
}

static void print_special_test_demo()
{
    printf("ChenYingXing:>touch file\n");
    printf("ChenYingXing:>test_fill file 2200000000\n");
    printf("test_fill: 耗时 156646ms\n");
    printf("test_fill: 完成\n");
    printf("ChenYingXing:>ls -lh\n");
    printf("1  2G  2026-04-28 15:17:59  file\n");
    printf("ChenYingXing:>mkdir dir1\n");
    printf("ChenYingXing:>test_churn ./dir1 1000 64 8192\n");
    printf("test_churn: 完成\n");
    printf("  create_err=0, write_err=0, delete_err=0\n");
    printf("  耗时 297610ms\n");
    printf("ChenYingXing:>\n");
    printf("ChenYingXing:>mkdir dir2\n");
    printf("ChenYingXing:>test_mkdir_deep . 4096\n");
    printf("test_mkdir_deep: 完成\n");
    printf("  created=4096, exists=0, failed=0\n");
    printf("  耗时 4785ms\n");
    printf("ChenYingXing:>mkdir dir3\n");
    printf("ChenYingXing:>test_mkfiles ./dir3 50000\n");
    printf("test_mkfiles: 完成\n");
    printf("  created=10000, failed=0\n");
    printf("  耗时 1642527ms\n");
}

// 内核主函数
extern "C" void kernelMain(void * multiboot_structure, int32_t magic_number)
{
    fb_console_init(multiboot_structure, static_cast<uint32_t>(magic_number));

    // printf("Hello Ext4 World!\n");
    display_banner();

    // 初始化全局描述符表
    GlobalDescriptorTable gdt;

    // 初始化任务管理器
    TaskManager taskManager;

    // 初始化中断管理器
    InterruptManager interrupts(&gdt, &taskManager);

    // 初始化 PIT 为 1000Hz，提供毫秒级 tick。
    pit_init(1000);

    // 初始化系统调用处理器
    SyscallHandler syscalls(&interrupts);
    
    // 初始化驱动管理器
    DriverManager driverManager;

    // 创建键盘事件处理器和驱动
    PrintfKeyboardEventHandler kbhandler;
    KeyboardDriver keyboard(&interrupts, &kbhandler); 

    // 创建鼠标事件处理器和驱动（已注释）
    //MouseToConsole mousehandler;
    //MouseDriver mouse(&interrupts, &mousehandler);

    // 注册并激活驱动
    driverManager.AddDriver(&keyboard);
    //driverManager.AddDriver(&mouse);
    driverManager.Activate();

    // 激活中断
    interrupts.Activate();

    // 初始化 ATA 驱动并向块层注册磁盘
    ata_init();
    ata_disk_register();

    // 注册 Ext4 文件系统
    printf("正在注册 Ext4 文件系统...\n");
    if (register_filesystem(&ext4_fs_type) == 0) {
        printf("Ext4 文件系统注册成功\n");
    } else {
        printf("Ext4 文件系统注册失败\n");
    }

    // 挂载 Ext4
    printf("正在挂载 Ext4 文件系统...\n");
    struct dentry *root = ext4_fs_type.mount(&ext4_fs_type, 0, nullptr, nullptr);
    if (root) {
        printf("Ext4 文件系统挂载成功\n");
        printf("根索引节点已就绪\n");
    } else {
        printf("Ext4 文件系统挂载失败\n");
    }

    // print_special_test_demo();
    // 显示Shell提示符
    printf(SHELL_PROMPT);

    // 主循环：轮询执行已提交的一行命令
    while (1) {
        simpleShell(&keyboard);
    }
}
