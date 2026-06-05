#ifndef __LIB_TIME_H_
#define __LIB_TIME_H_

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * format_time - 将“自 1970-01-01 起的秒数”格式化为 "YYYY-MM-DD hh:mm:ss"
 * 仅使用 32 位运算，忽略闰秒，按 UTC 计算。
 */
void format_time(unsigned long secs, char *buf, int buf_size);

/* Linux date 默认输出，例如 "Thu Jun  4 22:03:00 UTC 2026" */
void format_date_default(unsigned long secs, char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif
