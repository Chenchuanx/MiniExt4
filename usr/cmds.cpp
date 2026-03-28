#include <usr/cmds.h>
#include <lib/syscall.h>
#include <linux/dirent.h>
#include <linux/string.h>
#include <linux/fs.h>

static void cmd_time(const int8_t *arg) {
	(void)arg;
	sysTime();
}

static void cmd_pwd(const int8_t *arg) {
	(void)arg;
	char buf[256];
	int ret = sysGetcwd(buf, sizeof(buf));
	if (ret == 0) {
		sysPrintf((int8_t *)buf);
		sysPrintf((int8_t *)"\n");
	} else {
		sysPrintf((int8_t *)"pwd: error\n");
	}
}

static void cmd_ls(const int8_t *arg) {
	const char *path = arg ? (const char *)arg : ".";
	int fd = sysOpen(path, 0, 0);
	if (fd < 0) {
		sysPrintf((int8_t *)"ls: cannot access path\n");
		return;
	}

	char buf[1024];
	int nread;

	while ((nread = sysGetdents(fd, buf, sizeof(buf))) > 0) {
		int bpos = 0;
		while (bpos < nread) {
			struct linux_dirent *d = (struct linux_dirent *)(buf + bpos);

			int name_len = 0;
			while (d->d_name[name_len])
				name_len++;

			if (!((name_len == 1 && d->d_name[0] == '.') ||
			      (name_len == 2 && d->d_name[0] == '.' && d->d_name[1] == '.'))) {
				sysPrintf((int8_t *)d->d_name);
				sysPrintf((int8_t *)"\n");
			}

			bpos += d->d_reclen;
		}
	}

	sysClose(fd);
}

static void cmd_mkdir(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"mkdir: missing operand\n");
	} else {
		int ret = sysMkdir(arg);
		if (ret != 0) {
			sysPrintf((int8_t *)"mkdir: failed\n");
		}
	}
}

static void cmd_touch(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"touch: missing operand\n");
		return;
	}

	int fd = sysOpen((const char *)arg, O_CREAT | O_WRONLY, 0644);
	if (fd < 0) {
		sysPrintf((int8_t *)"touch: failed\n");
		return;
	}

	sysClose(fd);
}

static void cmd_cat(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"cat: missing operand\n");
		return;
	}

	const char *path = (const char *)arg;
	int fd = sysOpen(path, O_RDONLY, 0);
	if (fd < 0) {
		sysPrintf((int8_t *)"cat: cannot open file\n");
		sysPrintf((int8_t *)"\n");
		return;
	}

	char readbuf[256 + 1];
	int nread;
	while ((nread = sysFileRead(fd, readbuf, 256)) > 0) {
		readbuf[nread] = '\0';
		sysPrintf((int8_t *)readbuf);
	}

	if (nread < 0) {
		sysPrintf((int8_t *)"cat: read error\n");
		sysPrintf((int8_t *)"\n");
	}

	sysClose(fd);
}

static void cmd_rm(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"rm: missing operand\n");
		return;
	}

	int ret = sysUnlink(arg);
	if (ret != 0) {
		sysPrintf((int8_t *)"rm: failed\n");
	}
}

static unsigned long parse_ulong10(const char *s)
{
	unsigned long v = 0;
	if (!s || *s == '\0') {
		return (unsigned long)-1;
	}
	while (*s >= '0' && *s <= '9') {
		unsigned d = (unsigned)(*s - '0');
		if (v > ((unsigned long)-1 - d) / 10UL) {
			return (unsigned long)-1;
		}
		v = v * 10UL + d;
		s++;
	}
	if (*s != '\0') {
		return (unsigned long)-1;
	}
	return v;
}

static void cmd_test_fill(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"test_fill: usage: test_fill PATH BYTES\n");
		return;
	}

	const char *p = (const char *)arg;
	while (*p == ' ' || *p == '\t') {
		p++;
	}

	char path[128];
	int pi = 0;
	while (*p != '\0' && *p != ' ' && *p != '\t' && pi < (int)sizeof(path) - 1) {
		path[pi++] = *p++;
	}
	path[pi] = '\0';

	while (*p == ' ' || *p == '\t') {
		p++;
	}

	unsigned long nbytes = parse_ulong10(p);
	if (path[0] == '\0' || nbytes == (unsigned long)-1) {
		sysPrintf((int8_t *)"test_fill: usage: test_fill PATH BYTES\n");
		return;
	}

	int fd = sysOpen(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		sysPrintf((int8_t *)"test_fill: cannot open file\n");
		return;
	}

	char chunk[256];
	for (int i = 0; i < (int)sizeof(chunk); i++) {
		chunk[i] = (char)0x41;
	}

	while (nbytes > 0) {
		unsigned long chunk_len = nbytes > sizeof(chunk) ? sizeof(chunk) : nbytes;
		int w = sysFileWrite(fd, chunk, (int)chunk_len);
		if (w <= 0) {
			sysPrintf((int8_t *)"test_fill: write error\n");
			sysClose(fd);
			return;
		}
		nbytes -= (unsigned long)w;
	}

	sysClose(fd);
	sysPrintf((int8_t *)"test_fill: ok\n");
}

static void cmd_echo(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"\n");
		return;
	}

	const char *p = (const char *)arg;

	const char *redir = 0;
	for (const char *q = p; *q != '\0'; ++q) {
		if (*q == '>') {
			redir = q;
			break;
		}
	}

	if (!redir) {
		sysPrintf((int8_t *)p);
		sysPrintf((int8_t *)"\n");
		return;
	}

	char content[256];
	int clen = 0;
	const char *left_end = redir;
	while (left_end > p && (*(left_end - 1) == ' ' || *(left_end - 1) == '\t')) {
		left_end--;
	}
	const char *left = p;
	while (left < left_end && clen < (int)sizeof(content) - 1) {
		content[clen++] = *left++;
	}
	content[clen] = '\0';

	const char *fname = redir + 1;
	while (*fname == ' ' || *fname == '\t') {
		fname++;
	}
	if (*fname == '\0') {
		sysPrintf((int8_t *)"echo: missing filename after '>'\n");
		return;
	}

	char filename[256];
	int flen = 0;
	while (*fname != '\0' && *fname != ' ' && *fname != '\t' &&
	       flen < (int)sizeof(filename) - 1) {
		filename[flen++] = *fname++;
	}
	filename[flen] = '\0';

	if (flen == 0) {
		sysPrintf((int8_t *)"echo: invalid filename\n");
		return;
	}

	int fd = sysOpen(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		sysPrintf((int8_t *)"echo: cannot open file\n");
		return;
	}

	if (clen > 0) {
		int written = sysFileWrite(fd, content, clen);
		if (written < 0) {
			sysPrintf((int8_t *)"echo: write error\n");
			sysClose(fd);
			return;
		}
	}
	const char nl = '\n';
	sysFileWrite(fd, &nl, 1);

	sysClose(fd);
}

static void cmd_cd(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"cd: missing operand\n");
	} else {
		int ret = sysChdir(arg);
		if (ret != 0) {
			sysPrintf((int8_t *)"cd: failed\n");
		}
	}
}

const struct cmd_entry cmd_table[] = {
	{"time", cmd_time},
	{"pwd", cmd_pwd},
	{"ls", cmd_ls},
	{"mkdir", cmd_mkdir},
	{"cd", cmd_cd},
	{"touch", cmd_touch},
	{"echo", cmd_echo},
	{"cat", cmd_cat},
	{"rm", cmd_rm},
	{"test_fill", cmd_test_fill},
	{0, 0},
};
