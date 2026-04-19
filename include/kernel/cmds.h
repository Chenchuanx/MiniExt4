#ifndef __KERNEL_CMDS_H_
#define __KERNEL_CMDS_H_

#include <linux/types.h>

typedef void (*cmd_handler_t)(const int8_t *arg);

struct cmd_entry {
	const char *name;
	cmd_handler_t handler;
	const char *help; /* 单行简介，可为 NULL */
};

/* Terminated by { NULL, NULL, NULL } */
extern const struct cmd_entry cmd_table[];

#endif
