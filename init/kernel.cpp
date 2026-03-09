#include <linux/types.h>
#include <mm/gdt.h>
#include <kernel/interrupts.h>
#include <drivers/keyboard.h>
#include <drivers/driver.h>
#include <drivers/mouse.h>
#include <kernel/syscalls.h>
#include <lib/printf.h>
#include <usr/shell.h>
#include <drivers/handlers.h>
#include <drivers/ata.h>
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

// 内核主函数
extern "C" void kernelMain(void * multiboot_structure, int32_t magic_number)
{
    // 避免未使用参数的警告（暂未使用 multiboot 结构和 magic number）
    (void)multiboot_structure;
    (void)magic_number;
    
    // 显示启动信息
    printf("Hello Ext4 World!\n");

    // 初始化全局描述符表
    GlobalDescriptorTable gdt;

    // 初始化任务管理器
    TaskManager taskManager;

    // 初始化中断管理器
    InterruptManager interrupts(&gdt, &taskManager);

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

    // 初始化 ATA 驱动
    printf("Initializing ATA driver...\n");
    if (ata_init() == 0) {
        printf("ATA driver initialized successfully\n");
    } else {
        printf("ATA driver initialization failed\n");
    }

    // 注册 Ext4 文件系统
    printf("Registering Ext4 filesystem...\n");
    if (register_filesystem(&ext4_fs_type) == 0) {
        printf("Ext4 filesystem registered successfully\n");
    } else {
        printf("Ext4 filesystem registration failed\n");
    }

    // 尝试挂载 Ext4（简化版，直接挂载）
    printf("Mounting Ext4 filesystem...\n");
    struct dentry *root = ext4_fs_type.mount(&ext4_fs_type, 0, nullptr, nullptr);
    if (root) {
        printf("Ext4 filesystem mounted successfully\n");
        // 简化版：printf 不支持格式化，只显示基本信息
        printf("Root inode mounted\n");
    } else {
        printf("Ext4 filesystem mount failed\n");
    }

    // 显示Shell提示符
    printf(SHELL_PROMPT);

    // 主循环
    while(1);
}
