#ifndef __LIB_NUMFMT_H_
#define __LIB_NUMFMT_H_

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 将无符号整数转换为十进制字符串 */
void u32_to_dec(char *buf, int buf_size, unsigned long v);

/* 将 64 位无符号整数转换为十进制字符串（避免 64 位除法依赖） */
void u64_to_dec(char *buf, int buf_size, uint64_t v);

/* 将字节大小转换为可读格式（如 12K, 3.5M） */
void readable_size(unsigned long size, char *buf, int buf_size);

#ifdef __cplusplus
}
#endif

#endif
