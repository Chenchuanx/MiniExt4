#ifndef __SHELL_H_
#define __SHELL_H_

#include <linux/types.h>

class KeyboardDriver;

// Shell 提示符（全局常量）
extern const int8_t SHELL_PROMPT[];
extern const uint8_t SHELL_PROMPT_LEN;

// 字符串比较函数
int8_t strcmp(const int8_t * src, const int8_t * dest);

// 简单Shell命令处理
void simpleShell(const char c, KeyboardDriver * pKeyDriver);

#endif

