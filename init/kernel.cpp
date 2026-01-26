#include <linux/types.h>
#include <mm/gdt.h>
#include <kernel/interrupts.h>
#include <drivers/keyboard.h>
#include <drivers/driver.h>
#include <drivers/mouse.h>
#include <kernel/syscalls.h>
#include <lib/console.h>
#include <lib/shell.h>
#include <drivers/handlers.h>

// 构造函数类型定义
typedef void (*constructor)();
extern constructor start_ctors;
extern constructor end_ctors;

// 调用所有全局对象的构造函数
extern "C" void callConstructors()
{
    for(constructor * i = &start_ctors; i != &end_ctors; ++i)
        (*i)();
}

// 系统调用：打印字符串（外部声明）
extern void sysprintf(const int8_t * str);

// 内核主函数
extern "C" void kernelMain(void * multiboo_structure, int32_t magicnumber)
{
    // 设置光标到第15行
    setCursorPosition(0, 14);
    
    // 显示启动信息
    printf("Hello Ext4 World!\n");

    // 初始化全局描述符表
    GlobalDescriptorTable gdt;

    // 初始化任务管理器
    TaskManager taskManger;
    //Task task1(&gdt, taskA);
    //Task task2(&gdt, taskB);
    //taskManger.AddTask(&task1);
    //taskManger.AddTask(&task2);

    // 初始化中断管理器
    InterruptManager interrupts(&gdt, &taskManger);

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

    // 显示Shell提示符
    sysprintf("ChenYingXing:>");

    // 主循环
    while(1);
}
