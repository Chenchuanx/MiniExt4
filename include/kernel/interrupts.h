#ifndef __INTERRUPTS_H_
#define __INTERRUPTS_H_

#include <linux/types.h>
#include <lib/port.h>
#include <mm/gdt.h>
#include <kernel/multitasking.h>

class InterruptManager;

class InterruptHandler
{
protected:
    uint8_t interruptNumber;
    InterruptManager * interruptManager;
public:
    InterruptHandler(uint8_t interruptNumber, InterruptManager * InterruptHandler);
    ~InterruptHandler();

    virtual uint32_t HandleInterrupt(uint32_t esp);
};

class InterruptManager
{
    friend class InterruptHandler;

protected:
    InterruptHandler * handlers[256];
    TaskManager * taskManager;
    static InterruptManager * ActiveInterruptManager;

    struct GateDescriptor
    {
        uint16_t handlerAddressLowBits;   //中断处理程序入口地址低位
        uint16_t gdt_codeSegmentSelector; // 段选择址
        uint8_t reserved;                 //保留
        uint8_t access;                   //访问控制位
        uint16_t handlerAddressHighBits;  //高位

    } __attribute__((packed));

    static GateDescriptor interruptDescriptorTable[256]; //中断描述符表

    struct InterruptDescriptorTablePointer
    {
        uint16_t size; //大小
        uint32_t base; //基址
    } __attribute__((packed));

    static void SetInterruptDescriptorTableEntry(
        uint8_t interruptNumber,            //中断号
        uint16_t codeSegmentSelectorOffset, //段选择符
        void (*handler)(),                  //中断处理函数
        uint8_t DescriptorPrivilegelevel,   //描述符的特权集
        uint8_t DescriptorType             //描述符类型
    ); //初始化IDT表

    Port8BitSlow picMasterCommand; //主片命令端口
    Port8BitSlow picMasterData;    //主片数据端口
    Port8BitSlow picSlaveCommand;  //从片命令端口
    Port8BitSlow picSlaveData;     //从片数据端口

public:
    InterruptManager(GlobalDescriptorTable * gdt, TaskManager * taskManger);
    ~InterruptManager();

    void Activate(); //打开终端
    void Deactivate(); //关闭中断

    static uint32_t handleInterrupt(uint8_t interruptNumber, uint32_t esp);   //中断处理函数
    uint32_t DoHandleInterrupt(uint8_t interruptNumber, uint32_t esp);
    static void IgnoreInterruptRequest();
    static void HandleInterruptRequest0x00();
    static void HandleInterruptRequest0x01();
    static void HandleInterruptRequest0x0C();
    static void HandleInterruptRequest0x60();
};


#endif
