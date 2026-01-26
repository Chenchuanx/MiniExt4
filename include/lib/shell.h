#ifndef __SHELL_H_
#define __SHELL_H_

#include <linux/types.h>

class KeyboardDriver;

// 字符串比较函数
int8_t strcmp(const int8_t * src, const int8_t * dest);

// 简单Shell命令处理
void simpleShell(const char c, KeyboardDriver * pKeyDriver);

#endif

