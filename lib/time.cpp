#include <lib/time.h>

/*
 * 内部工具：将无符号整数转换为十进制字符串
 */
static void u32_to_dec_local(char *buf, int buf_size, unsigned long v)
{
    if (buf_size <= 1) {
        return;
    }

    char tmp[16];
    int pos = 0;

    if (v == 0U) {
        tmp[pos++] = '0';
    } else {
        while (v > 0U && pos < (int)sizeof(tmp)) {
            unsigned int d = v % 10U;
            tmp[pos++] = (char)('0' + d);
            v /= 10U;
        }
    }

    int out = 0;
    if (pos >= buf_size) {
        pos = buf_size - 1;
    }
    while (pos > 0) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
}

/*
 * format_time - 将“自 1970-01-01 起的秒数”格式化为 "YYYY-MM-DD hh:mm:ss"
 * 仅使用 32 位运算，忽略闰秒，按 UTC 计算。
 */
void format_time(unsigned long secs, char *buf, int buf_size)
{
    if (buf_size < 20) {
        if (buf_size > 0) {
            buf[0] = '\0';
        }
        return;
    }

    unsigned long days = secs / 86400UL;
    unsigned long rem  = secs % 86400UL;

    unsigned long hour = rem / 3600UL;
    rem %= 3600UL;
    unsigned long min = rem / 60UL;
    unsigned long sec = rem % 60UL;

    /* 计算年/月/日，从 1970 年开始 */
    unsigned long year = 1970;
    for (;;) {
        int leap = ((year % 4UL == 0 && year % 100UL != 0) || (year % 400UL == 0)) ? 1 : 0;
        unsigned long ydays = (unsigned long)(leap ? 366 : 365);
        if (days >= ydays) {
            days -= ydays;
            year++;
        } else {
            break;
        }
    }

    int month_lengths[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = ((year % 4UL == 0 && year % 100UL != 0) || (year % 400UL == 0)) ? 1 : 0;
    if (leap) {
        month_lengths[1] = 29;
    }

    int month = 0;
    while (month < 12 && days >= (unsigned long)month_lengths[month]) {
        days -= (unsigned long)month_lengths[month];
        month++;
    }

    unsigned long day = days + 1;       /* 从 1 开始 */
    unsigned long mon = (unsigned long)(month + 1); /* 1-12 */

    /* 填充到 buf，格式 YYYY-MM-DD hh:mm:ss */
    int pos = 0;
    char tmp[16];

    /* 年 */
    u32_to_dec_local(tmp, sizeof(tmp), year);
    {
        int i = 0;
        while (tmp[i] && pos < buf_size - 1) {
            buf[pos++] = tmp[i++];
        }
    }
    buf[pos++] = '-';

    /* 月，两位 */
    if (mon < 10UL) {
        buf[pos++] = '0';
        buf[pos++] = (char)('0' + (int)mon);
    } else {
        u32_to_dec_local(tmp, sizeof(tmp), mon);
        buf[pos++] = tmp[0];
        buf[pos++] = tmp[1];
    }
    buf[pos++] = '-';

    /* 日，两位 */
    if (day < 10UL) {
        buf[pos++] = '0';
        buf[pos++] = (char)('0' + (int)day);
    } else {
        u32_to_dec_local(tmp, sizeof(tmp), day);
        buf[pos++] = tmp[0];
        buf[pos++] = tmp[1];
    }

    buf[pos++] = ' ';

    /* 时，两位 */
    if (hour < 10UL) {
        buf[pos++] = '0';
        buf[pos++] = (char)('0' + (int)hour);
    } else {
        u32_to_dec_local(tmp, sizeof(tmp), hour);
        buf[pos++] = tmp[0];
        buf[pos++] = tmp[1];
    }
    buf[pos++] = ':';

    /* 分，两位 */
    if (min < 10UL) {
        buf[pos++] = '0';
        buf[pos++] = (char)('0' + (int)min);
    } else {
        u32_to_dec_local(tmp, sizeof(tmp), min);
        buf[pos++] = tmp[0];
        buf[pos++] = tmp[1];
    }
    buf[pos++] = ':';

    /* 秒，两位 */
    if (sec < 10UL) {
        buf[pos++] = '0';
        buf[pos++] = (char)('0' + (int)sec);
    } else {
        u32_to_dec_local(tmp, sizeof(tmp), sec);
        buf[pos++] = tmp[0];
        buf[pos++] = tmp[1];
    }

    if (pos >= buf_size) {
        pos = buf_size - 1;
    }
    buf[pos] = '\0';
}

