# include <kernel/syscalls.h>
# include <lib/console.h>
# include <kernel/time.h>
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
    case 1:
        time();
        break;
    case 4:
        printf((int8_t*)cpu->ebx);
        break;
    default:
        break;
    }

    return esp; 
}