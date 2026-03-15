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
void rtc_read_time(struct rtc_time *t);

#ifdef __cplusplus
extern "C" {
#endif
/* 返回当前时间的 Unix 时间戳（秒，自 1970-01-01 00:00:00 UTC）*/
uint32_t rtc_get_unix_time(void);
#ifdef __cplusplus
}
#endif

#endif

