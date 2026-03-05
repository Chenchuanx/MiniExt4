#include <drivers/rtc.h>
#include <lib/port.h>

// 从 CMOS RTC 读取当前时间（简单轮询方式，不处理 BCD/24 小时制等细节）
void rtc_read_time(rtc_time *t)
{
    if (!t) {
        return;
    }

    Port8Bit out(0x70), in(0x71);  // CMOS RTC 端口

    // 小时
    out.Write(4);
    t->hour = in.Read();

    // 分钟
    out.Write(2);
    t->minute = in.Read();

    // 秒
    out.Write(0);
    t->second = in.Read();
}

