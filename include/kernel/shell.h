#ifndef __KERNEL_SHELL_H_
#define __KERNEL_SHELL_H_

#include <linux/types.h>
#include <drivers/keyboard.h>

extern const int8_t SHELL_PROMPT[];
extern const uint8_t SHELL_PROMPT_LEN;

void simpleShell(const char c, KeyboardDriver * pKeyDriver);

#endif
