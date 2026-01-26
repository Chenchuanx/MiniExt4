#include <kernel/multitasking.h>
#include <linux/types.h>

Task::Task(GlobalDescriptorTable * gdt, void entrypoint())
{
    cpustate = (CPUState*)(stack + 4096 - sizeof(CPUState));  //栈顶位置

    cpustate->eax = 0;
    cpustate->ebx = 0;
    cpustate->ecx = 0;
    cpustate->edx = 0;

    cpustate->esi = 0;
    cpustate->edi = 0;
    cpustate->ebp = 0;

    /*
    cpustate->gs = 0;
    cpustate->fs = 0;
    cpustate->es = 0;
    cpustate->ds = 0;
    */
   
   cpustate->error = 0;

   cpustate->eip = (uint32_t)entrypoint; // 函数入口
   cpustate->cs = gdt->CodeSegmentSelector();
   cpustate->eflags = 0x202;
   // cpustate->esp = 0;
   // cpustate->ss = 0;
}

Task::~Task()
{

}

TaskManager::TaskManager() : numTasks(0), currentTask(-1)
{

}

TaskManager::~TaskManager()
{

}

bool TaskManager::AddTask(Task * task)
{
    if (numTasks >= 255)
    {
        return false;
    }
    else
    {
        tasks[numTasks++] = task;
        return true; 
    }
}

CPUState * TaskManager::Schedule(CPUState * cpustate)
{
    if (numTasks <= 0)  // 无任务
    {
        return cpustate;
    }

    if (currentTask >= 0)  //保存
    {
        tasks[currentTask]->cpustate = cpustate;
    }

    if (++currentTask >= numTasks)
    {
        currentTask = 0;
    }

    return tasks[currentTask]->cpustate;
}
