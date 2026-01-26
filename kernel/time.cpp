#include <kernel/time.h>
#include <lib/port.h>
#include <lib/console.h>

// 从CMOS RTC读取并显示当前时间
void time()
{
    Port8Bit out(0x70), in(0x71);  // CMOS RTC端口
    
    // 读取小时
    uint8_t idx = 4;               // 偏移，时
    out.Write(idx);
    idx = in.Read();
    printfHex(idx);
    printf(":");

    // 读取分钟
    idx = 2;                        // 偏移，分
    out.Write(idx);
    idx = in.Read();
    printfHex(idx);
    printf(":");

    // 读取秒
    idx = 0;                        // 偏移，秒
    out.Write(idx);
    idx = in.Read();
    printfHex(idx);
    printf("\n");
}

