#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

/* 标准整数类型 */
typedef char                   int8_t;
typedef unsigned char          uint8_t;         

typedef short                  int16_t;
typedef unsigned short         uint16_t;

typedef int                    int32_t;
typedef unsigned int           uint32_t;

typedef long long int          int64_t;
typedef unsigned long long int uint64_t;

typedef uint8_t                u8;
typedef uint16_t               u16;
typedef uint32_t               u32;
typedef uint64_t               u64;

typedef int8_t                 s8;
typedef int16_t                s16;
typedef int32_t                s32;
typedef int64_t                s64;

typedef uint32_t               dev_t;

typedef int64_t                loff_t;

typedef int64_t                time64_t;

typedef uint32_t               size_t;
typedef int32_t                ssize_t;

typedef uint32_t               uintptr_t;
typedef int32_t                intptr_t;

/* bool 类型定义 */
#ifndef __cplusplus
typedef int bool;
#define true 1
#define false 0
#endif

#endif /* __LINUX_TYPES_H */