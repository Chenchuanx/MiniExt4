#ifndef __RTC_H_
#define __RTC_H_

#include <linux/types.h>

struct rtc_time
{
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

// 读取 CMOS RTC 当前时间（简单轮询方式）
void rtc_read_time(rtc_time *t);

#endif

