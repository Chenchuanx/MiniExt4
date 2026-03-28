#include <usr/shell.h>
#include <usr/cmds.h>
#include <drivers/keyboard.h>
#include <lib/syscall.h>
#include <linux/string.h>

const int8_t SHELL_PROMPT[] = "ChenYingXing:>";
const uint8_t SHELL_PROMPT_LEN = sizeof(SHELL_PROMPT) - 1;

static void parse_command(int8_t *cmd, int8_t **name, const int8_t **arg)
{
	while (*cmd == ' ') {
		cmd++;
	}

	if (*cmd == '\0') {
		*name = 0;
		*arg = 0;
		return;
	}

	*name = cmd;

	while (*cmd != '\0' && *cmd != ' ') {
		cmd++;
	}

	if (*cmd == '\0') {
		*arg = 0;
		return;
	}

	*cmd = '\0';
	cmd++;

	while (*cmd == ' ') {
		cmd++;
	}

	if (*cmd == '\0') {
		*arg = 0;
	} else {
		*arg = cmd;
	}
}

void simpleShell(const char c, KeyboardDriver *pKeyDriver)
{
	if (c != '\n') {
		return;
	}

	int8_t cmd[256] = {0};
	pKeyDriver->get_buffer(cmd);

	int8_t *cmd_name = 0;
	const int8_t *cmd_arg = 0;

	parse_command(cmd, &cmd_name, &cmd_arg);

	if (cmd_name != 0) {
		bool found = false;
		for (int i = 0; cmd_table[i].name != 0; i++) {
			if (strcmp(cmd_name, cmd_table[i].name) == 0) {
				cmd_table[i].handler(cmd_arg);
				found = true;
				break;
			}
		}

		if (!found) {
			sysPrintf((int8_t *)"unknown command: ");
			sysPrintf(cmd_name);
			sysPrintf((int8_t *)"\n");
		}
	}

	sysPrintf(SHELL_PROMPT);
}
