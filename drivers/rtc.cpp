#include <drivers/rtc.h>
#include <lib/port.h>

// 从 CMOS RTC 读取当前时间（简单轮询方式，不处理 BCD/24 小时制等细节）
void rtc_read_time(struct rtc_time *t)
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

// BCD 转二进制（若 Status Register B 的 bit2 为 0 则 RTC 为 BCD）
static inline uint8_t bcd_to_bin(uint8_t val, bool use_bcd)
{
    if (!use_bcd)
        return val;
    return (uint8_t)((val & 0x0F) + ((val >> 4) * 10));
}

// 是否为闰年
static inline bool is_leap_year(uint32_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 返回 1970-01-01 00:00:00 UTC 到给定日期的秒数（C 链接供 inode.c 调用）
extern "C" uint32_t rtc_get_unix_time(void)
{
    Port8Bit out(0x70), in(0x71);

    out.Write(0x0B);
    uint8_t reg_b = in.Read();
    bool use_bcd = (reg_b & 4) == 0;  // bit2: 0=BCD, 1=binary

    out.Write(0x00);
    uint8_t sec   = bcd_to_bin(in.Read(), use_bcd);
    out.Write(0x02);
    uint8_t min   = bcd_to_bin(in.Read(), use_bcd);
    out.Write(0x04);
    uint8_t hour  = bcd_to_bin(in.Read(), use_bcd);
    out.Write(0x07);
    uint8_t day   = bcd_to_bin(in.Read(), use_bcd);
    out.Write(0x08);
    uint8_t month = bcd_to_bin(in.Read(), use_bcd);
    out.Write(0x09);
    uint8_t year_byte = bcd_to_bin(in.Read(), use_bcd);

    uint8_t century_byte = 0;
    out.Write(0x32);
    century_byte = in.Read();
    // 若世纪寄存器无效（常见为 0），则按 2 位数年份推断：00–99 视为 2000–2099
    uint32_t year = (century_byte >= 19 && century_byte <= 21)
        ? (uint32_t)century_byte * 100 + year_byte
        : (uint32_t)2000 + year_byte;

    if (month < 1) month = 1;
    if (month > 12) month = 12;
    if (day < 1) day = 1;

    const uint8_t days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    uint32_t days = 0;
    for (uint32_t y = 1970; y < year; ++y)
        days += is_leap_year(y) ? 366 : 365;
    for (uint32_t m = 0; m < (uint32_t)(month - 1); ++m)
        days += days_in_month[m];
    if (month > 2 && is_leap_year(year))
        days += 1;
    days += (uint32_t)(day - 1);

    uint32_t secs = days * 86400u;
    secs += (uint32_t)hour * 3600u;
    secs += (uint32_t)min * 60u;
    secs += (uint32_t)sec;
    return secs;
}

