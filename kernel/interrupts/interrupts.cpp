# include <kernel/interrupts.h>

void printf(const int8_t * );
void printfHex(const uint8_t);

InterruptHandler::InterruptHandler(uint8_t interruptNumber, InterruptManager * interruptManager)
    : interruptNumber(interruptNumber), interruptManager(interruptManager)
{
    interruptManager->handlers[interruptNumber] = this;
}

InterruptHandler::~InterruptHandler()
{
    if(interruptManager->handlers[interruptNumber] == this)
    {
        interruptManager->handlers[interruptNumber] = 0;
    }
}

uint32_t InterruptHandler::HandleInterrupt(uint32_t esp)
{
    return esp;
}

InterruptManager::GateDescriptor InterruptManager::interruptDescriptorTable[256]; //中断描述符表

InterruptManager * InterruptManager::ActiveInterruptManager = 0;

InterruptManager::InterruptManager(GlobalDescriptorTable * gdt, TaskManager * taskManager)
    : picMasterCommand(0x20), picMasterData(0x21), picSlaveCommand(0xA0), picSlaveData(0xA1), taskManager(taskManager)
{
    uint16_t CodeSegment = gdt->CodeSegmentSelector();
    const uint8_t IDT_INTERRUPT_GATE = 0xE; //中断门标志

    for(uint16_t  i = 0; i < 256; ++i) //初始化IDT表
    {
        handlers[i] = 0;
        SetInterruptDescriptorTableEntry(i, CodeSegment, &IgnoreInterruptRequest, 0, IDT_INTERRUPT_GATE);
    }
    SetInterruptDescriptorTableEntry(0x21, CodeSegment, &HandleInterruptRequest0x01, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x20, CodeSegment, &HandleInterruptRequest0x00, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x2C, CodeSegment, &HandleInterruptRequest0x0C, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x80, CodeSegment, &HandleInterruptRequest0x60, 0, IDT_INTERRUPT_GATE);

    picMasterCommand.Write(0x11);
    picSlaveCommand.Write(0x11);

    picMasterData.Write(0x20); //
    picSlaveData.Write(0x28);

    picMasterData.Write(0x04); //输出数据
    picSlaveData.Write(0x02);

    picMasterData.Write(0x01);
    picSlaveData.Write(0x01);

    picMasterData.Write(0x00); //中断屏蔽码
    picSlaveData.Write(0x00);

    InterruptDescriptorTablePointer idt; //寄存器
    idt.size = 256 * sizeof(GateDescriptor) - 1;
    idt.base = (uint32_t)interruptDescriptorTable; //idt表首地址
    __asm__ volatile("lidt %0": : "m" (idt));
}

InterruptManager::~InterruptManager()
{
    
}

void InterruptManager::Activate()
{
    if(ActiveInterruptManager != nullptr)
    {
        ActiveInterruptManager->Deactivate();
    }
    ActiveInterruptManager = this;

    __asm__ volatile("sti");
}

void InterruptManager::Deactivate()
{
    if(ActiveInterruptManager != this)
    {
        ActiveInterruptManager = 0;
        __asm__ volatile("cli");
    }
    
}

void InterruptManager::SetInterruptDescriptorTableEntry(
        uint8_t interruptNumber,            //中断号
        uint16_t codeSegmentSelectorOffset, //段选择符
        void (*handler)(),                  //中断处理函数
        uint8_t DescriptorPrivilegelevel,   //描述符的特权集
        uint8_t DescriptorType              //描述符类型
        ) //初始化IDT表
{
    const uint8_t IDT_DESC_PRESENT = 0x80;
    interruptDescriptorTable[interruptNumber].handlerAddressLowBits = ((uint32_t)handler) & 0xFFFF;
    interruptDescriptorTable[interruptNumber].handlerAddressHighBits = ((uint32_t)handler >> 16) & 0xFFFF;
    interruptDescriptorTable[interruptNumber].gdt_codeSegmentSelector = codeSegmentSelectorOffset;
    interruptDescriptorTable[interruptNumber].access = IDT_DESC_PRESENT | DescriptorType | ((DescriptorPrivilegelevel & 3) << 5);
    interruptDescriptorTable[interruptNumber].reserved = 0;
}


uint32_t InterruptManager::handleInterrupt(uint8_t interruptNumber, uint32_t esp)   //中断处理函数
{
    if(ActiveInterruptManager != 0)
    {
        return ActiveInterruptManager->DoHandleInterrupt(interruptNumber, esp);
    }
    //printf(" interrupt");
    //Port8BitSlow command(0x20);
    //command.Write(0x20);

    return esp;
}

// 用户态—>核心态：中断处理函数通过寄存器的值来决定执行的操作
uint32_t InterruptManager::DoHandleInterrupt(uint8_t interruptNumber, uint32_t esp)
{

    if(handlers[interruptNumber] != nullptr)
    {
        esp = handlers[interruptNumber]->HandleInterrupt(esp);
    }
    else if(interruptNumber != 0x20)
    {
        char msg[] = " unhandled interrupt 0x00";
        //const char * hex = "0123456789ABCDEF";
        //msg[23] = hex[(interruptNumber >> 4) & 0x0F];
        //msg[24] = hex[interruptNumber & 0x0F];
        //print(msg)
        printfHex(interruptNumber);
    }

    if (interruptNumber == 0x20)
    {
        esp = (uint32_t)taskManager->Schedule((CPUState*)esp);
    }
    
    if(0x20 <= interruptNumber && interruptNumber < 0x30)
    {
        picMasterCommand.Write(0x20);
        if(0x28 <= interruptNumber)
        {
            picSlaveCommand.Write(0x20);
        }
    }

    return esp;
}