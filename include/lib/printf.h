#ifndef __PRINTF_H_
#define __PRINTF_H_

#include <linux/types.h>
#include <lib/numfmt.h>

#ifdef __cplusplus
extern "C" {
#endif

// 向屏幕输出字符串
void printf(const int8_t * str);

// 以十六进制格式输出数字
void printfHex(const uint8_t num);

#ifdef __cplusplus
}

static inline void printf_u64(uint64_t v)
{
    char buf[32];
    u64_to_dec(buf, (int)sizeof(buf), v);
    printf((const int8_t *)buf);
}

static inline void printf_i64(int64_t v)
{
    if (v < 0) {
        printf((const int8_t *)"-");
        printf_u64((uint64_t)(-(v + 1)) + 1ULL);
        return;
    }
    printf_u64((uint64_t)v);
}

static inline void printf(unsigned int v)
{
    printf_u64((uint64_t)v);
}

static inline void printf(unsigned long v)
{
    printf_u64((uint64_t)v);
}

static inline void printf(unsigned long long v)
{
    printf_u64((uint64_t)v);
}

static inline void printf(int v)
{
    printf_i64((int64_t)v);
}

static inline void printf(long v)
{
    printf_i64((int64_t)v);
}

static inline void printf(long long v)
{
    printf_i64((int64_t)v);
}

// 固定二参接口：前缀文本 + 数字
static inline void printf(const int8_t *prefix, unsigned long long v)
{
    printf(prefix);
    printf_u64((uint64_t)v);
}

static inline void printf(const int8_t *prefix, long long v)
{
    printf(prefix);
    printf_i64((int64_t)v);
}

static inline void printf(const int8_t *prefix, unsigned long v)
{
    printf(prefix, (unsigned long long)v);
}

static inline void printf(const int8_t *prefix, long v)
{
    printf(prefix, (long long)v);
}

static inline void printf(const int8_t *prefix, unsigned int v)
{
    printf(prefix, (unsigned long long)v);
}

static inline void printf(const int8_t *prefix, int v)
{
    printf(prefix, (long long)v);
}
#endif

#endif

