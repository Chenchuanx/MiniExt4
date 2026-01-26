#ifndef __CONSOLE_H_
#define __CONSOLE_H_

#include <linux/types.h>

// 设置VGA硬件光标位置
void setCursorPosition(uint8_t x, uint8_t y);

// 向屏幕输出字符串
void printf(const int8_t * str);

// 以十六进制格式输出数字
void printfHex(const uint8_t num);

#endif

