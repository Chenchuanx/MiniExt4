#ifndef __KERNEL_CMDS_H_
#define __KERNEL_CMDS_H_

#include <linux/types.h>

typedef void (*cmd_handler_t)(const int8_t *arg);

struct cmd_entry {
	const char *name;
	cmd_handler_t handler;
};

/* Terminated by { NULL, NULL } */
extern const struct cmd_entry cmd_table[];

#endif
