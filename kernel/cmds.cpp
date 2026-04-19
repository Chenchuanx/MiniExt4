#include "linux/types.h"
#include <drivers/rtc.h>
#include <kernel/cmds.h>
#include <lib/syscall.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <lib/time.h>

/*
 * u32_to_dec - 将无符号整数转换为十进制字符串
 *
 * @buf:    输出缓冲区
 * @buf_size: 缓冲区大小
 * @v:      要转换的无符号整数
 */
static void u32_to_dec(char *buf, int buf_size, unsigned long v)
{
    if (buf_size <= 1) {
        return;
    }

    char tmp[16];
    int pos = 0;

    if (v == 0U) {
        tmp[pos++] = '0';
    } else {
        while (v > 0U && pos < (int)sizeof(tmp)) {
            unsigned int d = v % 10U;
            tmp[pos++] = (char)('0' + d);
            v /= 10U;
        }
    }

    int out = 0;
    if (pos >= buf_size) {
        pos = buf_size - 1;
    }
    while (pos > 0) {
        buf[out++] = tmp[--pos];
    }
    buf[out] = '\0';
}

/*
 * readable_size - 将字节大小转换为可读的格式
 *
 * @size:    字节大小
 * @buf:    输出缓冲区
 * @buf_size: 缓冲区大小
 */
static void readable_size(unsigned long size, char *buf, int buf_size)
{
    const char *units[] = { "B", "K", "M", "G", "T" };
    int unit = 0;

    unsigned long whole = size;
    unsigned long rem = 0;

    /* 将 size 逐级换算到合适单位，同时保留余数用于一位小数 */
    while (whole >= 1024U && unit < 4) {
        rem = whole & 1023U;
        whole >>= 10; /* /1024 */
        unit++;
    }

    if (buf_size <= 1) {
        return;
    }

    /* B：不显示小数 */
    if (unit == 0) {
        char num[16];
        u32_to_dec(num, sizeof(num), whole);
        int i = 0, j = 0;
        while (num[i] != '\0' && j < buf_size - 2) {
            buf[j++] = num[i++];
        }
        if (j < buf_size - 1) {
            buf[j++] = units[unit][0];
            buf[j] = '\0';
        } else {
            buf[buf_size - 1] = '\0';
        }
        return;
    }

    /* 一位小数：decimal = round(rem * 10 / 1024) */
    unsigned long decimal = (rem * 10U + 512U) >> 10; /* /1024 */
    if (decimal >= 10U) {
        whole += 1U;
        decimal = 0U;
    }

    /* 组装字符串：whole[.decimal]unit */
    int j = 0;
    char num[16];
    u32_to_dec(num, sizeof(num), whole);
    for (int i = 0; num[i] != '\0' && j < buf_size - 1; i++) {
        buf[j++] = num[i];
    }

    if (decimal != 0U && j < buf_size - 2) {
        buf[j++] = '.';
        buf[j++] = (char)('0' + (int)decimal);
    }

    if (j < buf_size - 1) {
        buf[j++] = units[unit][0];
    }
    if (j >= buf_size) {
        j = buf_size - 1;
    }
    buf[j] = '\0';
}

/* VFS 失败为 -errno；打印时输出正的 errno 数值（见 include/linux/errno.h） */
static void print_errno_value(int err)
{
	char nbuf[16];
	unsigned long v;
	if (err < 0) {
		v = (unsigned long)(-err);
	} else {
		v = (unsigned long)err;
	}
	u32_to_dec(nbuf, sizeof(nbuf), v);
	sysPrintf((int8_t *)nbuf);
}

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
		sysPrintf((int8_t *)"pwd: 出错\n");
	}
}

static void cmd_ls(const int8_t *arg) {
    int show_long = 0;   // -l
    int human = 0;       // -h（仅在 show_long=1 时有效）
    int show_inode = 0;  // -i

    const char *p = arg ? (const char *)arg : 0;
    const char *path = ".";

    if (p && *p) {
        // 跳过前导空格
        while (*p == ' ' || *p == '\t') {
            p++;
        }

        // 解析多个选项组，例如：-l -h、-lh、-il 等
        while (*p == '-') {
            p++;
            while (*p && *p != ' ' && *p != '\t') {
                if (*p == 'l') {
                    show_long = 1;
                } else if (*p == 'h') {
                    human = 1;
                } else if (*p == 'i') {
                    show_inode = 1;
                }
                p++;
            }
            while (*p == ' ' || *p == '\t') {
                p++;
            }
        }

        // 剩下的非空部分当成路径
        if (*p) {
            path = p;
        }
    }

    int fd = sysOpen(path, 0, 0);
    if (fd < 0) {
        sysPrintf((int8_t *)"ls: ");
        sysPrintf((int8_t *)path);
        sysPrintf((int8_t *)": ");
        if (fd == -ENOENT) {
            sysPrintf((int8_t *)"找不到文件或目录\n");
        } else {
            sysPrintf((int8_t *)"失败，错误码=");
            print_errno_value(fd);
            sysPrintf((int8_t *)"\n");
        }
        return;
    }

    char buf[1024];
    int nread;

    while ((nread = sysGetdents(fd, buf, sizeof(buf))) > 0) {
        int bpos = 0;
        while (bpos < nread) {
            struct linux_dirent *d = (struct linux_dirent *)(buf + bpos);

            const char *name = d->d_name;
            int name_len = 0;
            while (name[name_len]) {
                name_len++;
            }

            // 跳过 "." 和 ".."
            if (!((name_len == 1 && name[0] == '.') ||
                  (name_len == 2 && name[0] == '.' && name[1] == '.'))) {

                // -i：输出 inode
                if (show_inode) {
                    char ino_buf[16];
                    u32_to_dec(ino_buf, sizeof(ino_buf), (unsigned long)d->d_ino);
                    sysPrintf((int8_t *)ino_buf);
                    sysPrintf((int8_t *)"  ");
                }

                if (!show_long) {
                    // 普通模式，只输出文件名
                    sysPrintf((int8_t *)name);
                    sysPrintf((int8_t *)"\n");
                } else {
                    // 长格式，需要用 stat 获取属性
                    struct kstat st;
                    char fullpath[256];
                    int pos = 0;

                    // 组合 fullpath = path + "/" + name
                    // 特殊处理 path="." 或以 '/' 结尾的情况
                    if (path[0] == '.' && path[1] == '\0') {
                        // 当前目录，直接用名字
                        pos = 0;
                    } else {
                        const char *pp = path;
                        while (*pp && pos < (int)sizeof(fullpath) - 1) {
                            fullpath[pos++] = *pp++;
                        }
                        if (pos > 0 && fullpath[pos - 1] != '/' && pos < (int)sizeof(fullpath) - 1) {
                            fullpath[pos++] = '/';
                        }
                    }

                    int i;
                    for (i = 0; i < name_len && pos < (int)sizeof(fullpath) - 1; i++) {
                        fullpath[pos++] = name[i];
                    }
                    fullpath[pos] = '\0';

                    if (sysStat(fullpath, &st) == 0) {
                        char num_buf[16];

                        // 硬链接数
                        u32_to_dec(num_buf, sizeof(num_buf), (unsigned long)st.nlink);
                        sysPrintf((int8_t *)num_buf);
                        sysPrintf((int8_t *)"  ");

                        // 大小：是否人类可读
                        if (human) {
                            char sz_buf[16];
                            readable_size((unsigned long)st.size, sz_buf, sizeof(sz_buf));
                            sysPrintf((int8_t *)sz_buf);
                        } else {
                            u32_to_dec(num_buf, sizeof(num_buf), (unsigned long)st.size);
                            sysPrintf((int8_t *)num_buf);
                        }
                        sysPrintf((int8_t *)"  ");

                        // 修改时间：将“自 1970 起的秒数”格式化为 YYYY-MM-DD hh:mm:ss
                        char time_buf[32];
                        format_time((unsigned long)st.mtime_ns, time_buf, sizeof(time_buf));
                        sysPrintf((int8_t *)time_buf);
                        sysPrintf((int8_t *)"  ");

                        // 文件名
                        sysPrintf((int8_t *)name);
                        sysPrintf((int8_t *)"\n");
                    } else {
                        // stat 失败则退化为普通输出
                        sysPrintf((int8_t *)name);
                        sysPrintf((int8_t *)"\n");
                    }
                }
            }

            bpos += d->d_reclen;
        }
    }

    sysClose(fd);
}

static void cmd_mkdir(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"mkdir: 缺少参数\n");
	} else {
		int ret = sysMkdir(arg);
		if (ret != 0) {
			sysPrintf((int8_t *)"mkdir: ");
			sysPrintf((int8_t *)(const char *)arg);
			sysPrintf((int8_t *)": ");
			if (ret == -ENOENT) {
				sysPrintf((int8_t *)"找不到文件或目录\n");
			} else if (ret == -ENOTDIR) {
				sysPrintf((int8_t *)"父路径不是目录\n");
			} else {
				sysPrintf((int8_t *)"失败，错误码=");
				print_errno_value(ret);
				sysPrintf((int8_t *)"\n");
			}
		}
	}
}

static void cmd_touch(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"touch: 缺少参数\n");
		return;
	}

	int fd = sysOpen((const char *)arg, O_CREAT | O_WRONLY, 0644);
	if (fd < 0) {
		sysPrintf((int8_t *)"touch: ");
		sysPrintf((int8_t *)(const char *)arg);
		sysPrintf((int8_t *)": ");
		if (fd == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else if (fd == -ENOTDIR) {
			sysPrintf((int8_t *)"父路径不是目录\n");
		} else {
			sysPrintf((int8_t *)"失败，错误码=");
			print_errno_value(fd);
			sysPrintf((int8_t *)"\n");
		}
		return;
	}

	sysClose(fd);
}

static void cmd_cat(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"cat: 缺少参数\n");
		return;
	}

	const char *path = (const char *)arg;
	int fd = sysOpen(path, O_RDONLY, 0);
	if (fd < 0) {
		sysPrintf((int8_t *)"cat: ");
		sysPrintf((int8_t *)path);
		sysPrintf((int8_t *)": ");
		if (fd == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else {
			sysPrintf((int8_t *)"失败，错误码=");
			print_errno_value(fd);
			sysPrintf((int8_t *)"\n");
		}
		return;
	}

	char readbuf[256 + 1];
	int nread;
	while ((nread = sysFileRead(fd, readbuf, 256)) > 0) {
		readbuf[nread] = '\0';
		sysPrintf((int8_t *)readbuf);
	}

	if (nread < 0) {
		sysPrintf((int8_t *)"cat: 读取失败\n");
		sysPrintf((int8_t *)"\n");
	}

	sysClose(fd);
}

static void cmd_rm(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"rm: 缺少参数\n");
		return;
	}

	int ret = sysUnlink(arg);
	if (ret != 0) {
		sysPrintf((int8_t *)"rm: ");
		sysPrintf((int8_t *)(const char *)arg);
		sysPrintf((int8_t *)": ");
		if (ret == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else if (ret == -EISDIR) {
			sysPrintf((int8_t *)"无法删除目录\n");
		} else {
			sysPrintf((int8_t *)"失败，错误码=");
			print_errno_value(ret);
			sysPrintf((int8_t *)"\n");
		}
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
		sysPrintf((int8_t *)"test_fill: 用法: test_fill PATH BYTES\n");
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
		sysPrintf((int8_t *)"test_fill: 用法: test_fill PATH BYTES\n");
		return;
	}

	int fd = sysOpen(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		sysPrintf((int8_t *)"test_fill: ");
		sysPrintf((int8_t *)path);
		sysPrintf((int8_t *)": ");
		if (fd == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else if (fd == -ENOTDIR) {
			sysPrintf((int8_t *)"父路径不是目录\n");
		} else {
			sysPrintf((int8_t *)"失败，错误码=");
			print_errno_value(fd);
			sysPrintf((int8_t *)"\n");
		}
		return;
	}

	int64_t start_time = rtc_get_unix_time();

	static const int block_size = 64 * 1024;
	static char chunk[block_size];

	for (int i = 0; i < block_size; i++) {
		chunk[i] = (char)0x41;
	}

	unsigned long total_req = nbytes;
	unsigned long total_written = 0;
	while (nbytes > 0) {
		int to_write = (nbytes > (unsigned long)block_size)
			? block_size
			: (int)nbytes;

		int w = sysFileWrite(fd, chunk, to_write);
		if (w < 0) {
			sysPrintf((int8_t *)"test_fill: 写入失败（IO 错误）\n");
			char req_buf[16];
			char wrote_buf[16];
			u32_to_dec(req_buf, sizeof(req_buf), total_req);
			u32_to_dec(wrote_buf, sizeof(wrote_buf), total_written);
			sysPrintf((int8_t *)"  请求=");
			sysPrintf((int8_t *)req_buf);
			sysPrintf((int8_t *)" 字节，已写入=");
			sysPrintf((int8_t *)wrote_buf);
			sysPrintf((int8_t *)" 字节\n");
			sysClose(fd);
			return;
		}
		if (w == 0) {
			sysPrintf((int8_t *)"test_fill: 磁盘空间不足（可能已满）\n");
			char req_buf[16];
			char wrote_buf[16];
			u32_to_dec(req_buf, sizeof(req_buf), total_req);
			u32_to_dec(wrote_buf, sizeof(wrote_buf), total_written);
			sysPrintf((int8_t *)"  请求=");
			sysPrintf((int8_t *)req_buf);
			sysPrintf((int8_t *)" 字节，已写入=");
			sysPrintf((int8_t *)wrote_buf);
			sysPrintf((int8_t *)" 字节\n");
			sysClose(fd);
			return;
		}

		total_written += (unsigned long)w;
		nbytes -= (unsigned long)w;

	}

	int64_t end_time = rtc_get_unix_time();
	int64_t duration = end_time - start_time;

	char duration_buf[16];
	u32_to_dec(duration_buf, sizeof(duration_buf), (unsigned long)duration);
	sysPrintf((int8_t *)duration_buf);
	sysPrintf((int8_t *)"s\n");

	sysClose(fd);
	sysPrintf((int8_t *)"test_fill: 完成\n");
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
		sysPrintf((int8_t *)"echo: 在 '>' 后缺少文件名\n");
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
		sysPrintf((int8_t *)"echo: 无效文件名\n");
		return;
	}

	int fd = sysOpen(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		sysPrintf((int8_t *)"echo: ");
		sysPrintf((int8_t *)filename);
		sysPrintf((int8_t *)": ");
		if (fd == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else if (fd == -ENOTDIR) {
			sysPrintf((int8_t *)"父路径不是目录\n");
		} else {
			sysPrintf((int8_t *)"失败，错误码=");
			print_errno_value(fd);
			sysPrintf((int8_t *)"\n");
		}
		return;
	}

	if (clen > 0) {
		int written = sysFileWrite(fd, content, clen);
		if (written < 0) {
			sysPrintf((int8_t *)"echo: 写入失败\n");
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
		sysPrintf((int8_t *)"cd: 缺少参数\n");
	} else {
		int ret = sysChdir(arg);
		if (ret != 0) {
			sysPrintf((int8_t *)"cd: ");
			sysPrintf((int8_t *)(const char *)arg);
			sysPrintf((int8_t *)": ");
			if (ret == -ENOENT) {
				sysPrintf((int8_t *)"找不到文件或目录\n");
			} else if (ret == -ENOTDIR) {
				sysPrintf((int8_t *)"不是目录\n");
			} else {
				sysPrintf((int8_t *)"失败，错误码=");
				print_errno_value(ret);
				sysPrintf((int8_t *)"\n");
			}
		}
	}
}

static void cmd_help(const int8_t *arg);

const struct cmd_entry cmd_table[] = {
	{"help", cmd_help, "列出所有命令或查看命令帮助"},
	{"time", cmd_time, "显示当前 RTC 时间"},
	{"pwd", cmd_pwd, "显示当前工作目录"},
	{"ls", cmd_ls, "列出目录内容 [-l]详细 [-h]易读大小 [-i]显示inode"},
	{"mkdir", cmd_mkdir, "创建目录"},
	{"cd", cmd_cd, "切换工作目录"},
	{"touch", cmd_touch, "创建空文件（不存在时）"},
	{"echo", cmd_echo, "输出文本；支持 > 重定向到文件"},
	{"cat", cmd_cat, "显示文件内容"},
	{"rm", cmd_rm, "删除文件"},
	{"test_fill", cmd_test_fill, "性能测试: test_fill <路径> <字节数>"},
	{0, 0, 0},
};

static void cmd_help(const int8_t *arg)
{
	const char *a = arg ? (const char *)arg : 0;
	if (a) {
		while (*a == ' ' || *a == '\t') {
			a++;
		}
	}

	if (!a || *a == '\0') {
		sysPrintf((int8_t *)"可用命令：\n");
		for (int i = 0; cmd_table[i].name != 0; i++) {
			sysPrintf((int8_t *)"  ");
			sysPrintf((int8_t *)cmd_table[i].name);
			if (cmd_table[i].help) {
				sysPrintf((int8_t *)"\t- ");
				sysPrintf((int8_t *)cmd_table[i].help);
			}
			sysPrintf((int8_t *)"\n");
		}
		return;
	}

	for (int i = 0; cmd_table[i].name != 0; i++) {
		if (strcmp(a, cmd_table[i].name) == 0) {
			sysPrintf((int8_t *)cmd_table[i].name);
			sysPrintf((int8_t *)"\n");
			if (cmd_table[i].help) {
				sysPrintf((int8_t *)cmd_table[i].help);
				sysPrintf((int8_t *)"\n");
			}
			if (strcmp(a, "ls") == 0) {
				sysPrintf((int8_t *)"  选项：-l 长列表；-h 人类可读大小（配合 -l）；-i 显示 inode\n");
			} else if (strcmp(a, "echo") == 0) {
				sysPrintf((int8_t *)"  示例：echo hello > a.txt\n");
			} else if (strcmp(a, "test_fill") == 0) {
				sysPrintf((int8_t *)"  示例：test_fill /path/file 1048576\n");
			}
			return;
		}
	}

	sysPrintf((int8_t *)"help: 没有名为 \"");
	sysPrintf((int8_t *)a);
	sysPrintf((int8_t *)"\" 的命令\n");
}
