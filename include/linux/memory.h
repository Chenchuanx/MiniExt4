#ifndef _LINUX_MEMORY_H
#define _LINUX_MEMORY_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 不依赖标准库的内存操作（-nostdlib 环境） */
void *simple_memset(void *s, int c, size_t n);
void *simple_memcpy(void *dest, const void *src, size_t n);
int simple_memcmp(const void *s1, const void *s2, size_t n);
void *simple_memmove(void *dest, const void *src, size_t n);

#define memset simple_memset
#define memcpy simple_memcpy
#define memcmp simple_memcmp
#define memmove simple_memmove

#ifdef __cplusplus
}
#endif

#endif
