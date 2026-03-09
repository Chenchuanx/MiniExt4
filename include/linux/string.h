#ifndef _LINUX_STRING_H
#define _LINUX_STRING_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// 字符串比较
int8_t strcmp(const int8_t * src, const int8_t * dest);

// 字符串长度
int strlen(const char * s);

#ifdef __cplusplus
}
#endif

#endif