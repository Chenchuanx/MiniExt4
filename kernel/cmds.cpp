#include "linux/types.h"
#include <drivers/pit.h>
#include <drivers/rtc.h>
#include <kernel/cmds.h>
#include <lib/syscall.h>
#include <linux/dirent.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <fs/ext4/ext4.h>
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
 * u64_to_dec - 将 64 位无符号整数转换为十进制字符串
 *
 * 说明：
 *  - 在 32 位内核里避免使用 64 位除法/取模，防止引入 libgcc 运行时符号依赖。
 */
static void u64_to_dec(char *buf, int buf_size, uint64_t v)
{
	if (buf_size <= 1) {
		return;
	}

	static const uint64_t pow10[] = {
		10000000000000000000ULL,
		1000000000000000000ULL,
		100000000000000000ULL,
		10000000000000000ULL,
		1000000000000000ULL,
		100000000000000ULL,
		10000000000000ULL,
		1000000000000ULL,
		100000000000ULL,
		10000000000ULL,
		1000000000ULL,
		100000000ULL,
		10000000ULL,
		1000000ULL,
		100000ULL,
		10000ULL,
		1000ULL,
		100ULL,
		10ULL,
		1ULL
	};

	int out = 0;
	int started = 0;
	for (int i = 0; i < (int)(sizeof(pow10) / sizeof(pow10[0])); i++) {
		uint8_t d = 0;
		while (v >= pow10[i]) {
			v -= pow10[i];
			d++;
		}

		if (d != 0 || started || i == (int)(sizeof(pow10) / sizeof(pow10[0])) - 1) {
			if (out < buf_size - 1) {
				buf[out++] = (char)('0' + d);
			}
			started = 1;
		}
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
			sysPrintf((int8_t *)"这是目录，请使用 rmdir 删除空目录\n");
		} else if (ret == -ENOTDIR) {
			sysPrintf((int8_t *)"父路径不是目录\n");
		} else if (ret == -EPERM || ret == -EACCES) {
			sysPrintf((int8_t *)"权限不足，无法删除\n");
		} else if (ret == -EBUSY) {
			sysPrintf((int8_t *)"文件正在使用，无法删除\n");
		} else if (ret == -EIO) {
			sysPrintf((int8_t *)"删除失败：底层存储 I/O 错误\n");
		} else {
			sysPrintf((int8_t *)"删除失败\n");
		}
	}
}

static void cmd_rmdir(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"rmdir: 缺少参数\n");
		return;
	}

	int ret = sysRmdir(arg);
	if (ret != 0) {
		sysPrintf((int8_t *)"rmdir: ");
		sysPrintf((int8_t *)(const char *)arg);
		sysPrintf((int8_t *)": ");
		if (ret == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else if (ret == -ENOTDIR) {
			sysPrintf((int8_t *)"不是目录\n");
		} else if (ret == -ENOTEMPTY) {
			sysPrintf((int8_t *)"目录非空，无法删除\n");
		} else if (ret == -EBUSY) {
			sysPrintf((int8_t *)"目录正被使用，无法删除\n");
		} else if (ret == -EPERM || ret == -EACCES) {
			sysPrintf((int8_t *)"权限不足，无法删除\n");
		} else if (ret == -EIO) {
			sysPrintf((int8_t *)"删除失败：底层存储 I/O 错误\n");
		} else {
			sysPrintf((int8_t *)"删除失败\n");
		}
	}
}

enum parse_u64_result {
	PARSE_U64_OK = 0,
	PARSE_U64_EMPTY = 1,
	PARSE_U64_INVALID = 2,
	PARSE_U64_OVERFLOW = 3,
};

static int parse_u64_10(const char *s, uint64_t *out)
{
	uint64_t v = 0;
	if (!s || *s == '\0') {
		return PARSE_U64_EMPTY;
	}
	while (*s >= '0' && *s <= '9') {
		uint64_t d = (uint64_t)(*s - '0');
		/* UINT64_MAX = 18446744073709551615 */
		if (v > 1844674407370955161ULL ||
		    (v == 1844674407370955161ULL && d > 5ULL)) {
			return PARSE_U64_OVERFLOW;
		}
		v = (v << 3) + (v << 1) + d; /* v = v*10 + d */
		s++;
	}
	if (*s != '\0') {
		return PARSE_U64_INVALID;
	}
	*out = v;
	return PARSE_U64_OK;
}

static void cmd_test_fill(const int8_t *arg) {
	struct super_block *root_sb = vfs_get_root_sb();
	uint32_t prev_prealloc = 0;
	uint32_t prev_sync_batch = 0;
	int switched_perf = 0;

	if (!arg) {
		sysPrintf((int8_t *)"test_fill: 用法: test_fill <路径> <字节数>\n");
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

	uint64_t nbytes = 0;
	int parse_ret = parse_u64_10(p, &nbytes);
	if (path[0] == '\0') {
		sysPrintf((int8_t *)"test_fill: 用法: test_fill PATH BYTES\n");
		return;
	}
	if (parse_ret != PARSE_U64_OK) {
		if (parse_ret == PARSE_U64_OVERFLOW) {
			sysPrintf((int8_t *)"test_fill: BYTES 超过 64 位上限\n");
		} else {
			sysPrintf((int8_t *)"test_fill: 用法: test_fill PATH BYTES\n");
		}
		return;
	}

	if (root_sb && root_sb->s_magic == EXT4_SUPER_MAGIC) {
		prev_prealloc = ext4_get_prealloc_goal_len();
		prev_sync_batch = ext4_get_bg_sync_batch();
		ext4_set_prealloc_goal_len(EXT4_TUNING_PERF_PREALLOC_GOAL_LEN);
		ext4_set_bg_sync_batch(EXT4_TUNING_PERF_BG_SYNC_BATCH);
		switched_perf = 1;
	}

	int fd = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
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
		if (switched_perf) {
			ext4_set_prealloc_goal_len(prev_prealloc);
			ext4_set_bg_sync_batch(prev_sync_batch);
		}
		return;
	}

	uint32_t start_ticks = pit_get_ticks();
	uint32_t pit_hz = pit_get_frequency_hz();
	if (pit_hz == 0) {
		pit_hz = 1000;
	}

	static const int block_size = 64 * 1024;
	static char chunk[block_size];

	for (int i = 0; i < block_size; i++) {
		chunk[i] = (char)0x41;
	}

	uint64_t total_req = nbytes;
	uint64_t total_written = 0;
	while (nbytes > 0) {
		int to_write = (nbytes > (uint64_t)block_size)
			? block_size
			: (int)nbytes;

		int w = vfs_write(fd, chunk, (size_t)to_write);
		if (w < 0) {
			sysPrintf((int8_t *)"test_fill: 写入失败（IO 错误）\n");
			char req_buf[32];
			char wrote_buf[32];
			u64_to_dec(req_buf, sizeof(req_buf), total_req);
			u64_to_dec(wrote_buf, sizeof(wrote_buf), total_written);
			sysPrintf((int8_t *)"  请求=");
			sysPrintf((int8_t *)req_buf);
			sysPrintf((int8_t *)" 字节，已写入=");
			sysPrintf((int8_t *)wrote_buf);
			sysPrintf((int8_t *)" 字节\n");
			vfs_close(fd);
			if (switched_perf && root_sb && root_sb->s_magic == EXT4_SUPER_MAGIC) {
				(void)ext4_balloc_flush(root_sb);
				(void)ext4_sync_super_free_counts(root_sb);
				ext4_set_prealloc_goal_len(prev_prealloc);
				ext4_set_bg_sync_batch(prev_sync_batch);
			}
			return;
		}
		if (w == 0) {
			sysPrintf((int8_t *)"test_fill: 磁盘空间不足（可能已满）\n");
			char req_buf[32];
			char wrote_buf[32];
			u64_to_dec(req_buf, sizeof(req_buf), total_req);
			u64_to_dec(wrote_buf, sizeof(wrote_buf), total_written);
			sysPrintf((int8_t *)"  请求=");
			sysPrintf((int8_t *)req_buf);
			sysPrintf((int8_t *)" 字节，已写入=");
			sysPrintf((int8_t *)wrote_buf);
			sysPrintf((int8_t *)" 字节\n");
			vfs_close(fd);
			if (switched_perf && root_sb && root_sb->s_magic == EXT4_SUPER_MAGIC) {
				(void)ext4_balloc_flush(root_sb);
				(void)ext4_sync_super_free_counts(root_sb);
				ext4_set_prealloc_goal_len(prev_prealloc);
				ext4_set_bg_sync_batch(prev_sync_batch);
			}
			return;
		}

		total_written += (uint64_t)w;
		nbytes -= (uint64_t)w;

	}

	vfs_close(fd);

	if (switched_perf && root_sb && root_sb->s_magic == EXT4_SUPER_MAGIC) {
		(void)ext4_balloc_flush(root_sb);
		(void)ext4_sync_super_free_counts(root_sb);
		ext4_set_prealloc_goal_len(prev_prealloc);
		ext4_set_bg_sync_batch(prev_sync_batch);
	}

	uint32_t end_ticks = pit_get_ticks();
	uint32_t elapsed_ticks = end_ticks - start_ticks;
	uint32_t duration_ms = (elapsed_ticks * 1000u) / pit_hz;

	char duration_buf[16];
	u32_to_dec(duration_buf, sizeof(duration_buf), (unsigned long)duration_ms);
	sysPrintf((int8_t *)"test_fill: 耗时 ");
	sysPrintf((int8_t *)duration_buf);
	sysPrintf((int8_t *)"ms\n");
	sysPrintf((int8_t *)"test_fill: 完成\n");
}

static void cmd_ext4_mode(const int8_t *arg) {
	if (!arg || arg[0] == '\0') {
		sysPrintf((int8_t *)"ext4_mode: 当前 prealloc=");
		char num[16];
		u32_to_dec(num, sizeof(num), ext4_get_prealloc_goal_len());
		sysPrintf((int8_t *)num);
		sysPrintf((int8_t *)", sync_batch=");
		u32_to_dec(num, sizeof(num), ext4_get_bg_sync_batch());
		sysPrintf((int8_t *)num);
		sysPrintf((int8_t *)"\n");
		sysPrintf((int8_t *)"用法: ext4_mode safe|perf\n");
		return;
	}

	if (strcmp((const int8_t *)arg, (const int8_t *)"safe") == 0) {
		ext4_set_prealloc_goal_len(EXT4_TUNING_SAFE_PREALLOC_GOAL_LEN);
		ext4_set_bg_sync_batch(EXT4_TUNING_SAFE_BG_SYNC_BATCH);
		sysPrintf((int8_t *)"ext4_mode: 已切换到 safe\n");
		return;
	}

	if (strcmp((const int8_t *)arg, (const int8_t *)"perf") == 0) {
		ext4_set_prealloc_goal_len(EXT4_TUNING_PERF_PREALLOC_GOAL_LEN);
		ext4_set_bg_sync_batch(EXT4_TUNING_PERF_BG_SYNC_BATCH);
		sysPrintf((int8_t *)"ext4_mode: 已切换到 perf\n");
		return;
	}

	sysPrintf((int8_t *)"ext4_mode: 仅支持 safe|perf\n");
}

static void cmd_test_read(const int8_t *arg) {
	if (!arg) {
		sysPrintf((int8_t *)"test_read: 用法: test_read <路径>\n");
		return;
	}

	const char *p = (const char *)arg;
	while (*p == ' ' || *p == '\t') {
		p++;
	}
	if (*p == '\0') {
		sysPrintf((int8_t *)"test_read: 用法: test_read PATH\n");
		return;
	}

	char path[128];
	int pi = 0;
	while (*p != '\0' && *p != ' ' && *p != '\t' && pi < (int)sizeof(path) - 1) {
		path[pi++] = *p++;
	}
	path[pi] = '\0';

	struct kstat st;
	int sret = vfs_stat(path, &st);
	if (sret != 0) {
		sysPrintf((int8_t *)"test_read: ");
		sysPrintf((int8_t *)path);
		sysPrintf((int8_t *)": ");
		if (sret == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else if (sret == -ENOTDIR) {
			sysPrintf((int8_t *)"父路径不是目录\n");
		} else {
			sysPrintf((int8_t *)"无法获取文件属性\n");
		}
		return;
	}

	if (S_ISDIR(st.mode)) {
		sysPrintf((int8_t *)"test_read: 目标是目录，请指定文件路径\n");
		return;
	}

	int fd = vfs_open(path, O_RDONLY, 0);
	if (fd < 0) {
		sysPrintf((int8_t *)"test_read: ");
		sysPrintf((int8_t *)path);
		sysPrintf((int8_t *)": ");
		if (fd == -ENOENT) {
			sysPrintf((int8_t *)"找不到文件或目录\n");
		} else if (fd == -ENOTDIR) {
			sysPrintf((int8_t *)"父路径不是目录\n");
		} else {
			sysPrintf((int8_t *)"无法打开文件\n");
		}
		return;
	}

	uint32_t start_ticks = pit_get_ticks();
	uint32_t pit_hz = pit_get_frequency_hz();
	if (pit_hz == 0) {
		pit_hz = 1000;
	}

	static const int block_size = 64 * 1024;
	static char chunk[block_size];
	uint64_t total_read = 0;
	uint64_t expected = (uint64_t)st.size;
	int reached_eof = 0;

	for (;;) {
		int r = vfs_read(fd, chunk, (size_t)block_size);
		if (r < 0) {
			sysPrintf((int8_t *)"test_read: 读取失败（IO 错误）\n");
			vfs_close(fd);
			return;
		}
		if (r == 0) {
			reached_eof = 1;
			break;
		}
		total_read += (uint64_t)r;
	}

	uint32_t end_ticks = pit_get_ticks();
	uint32_t elapsed_ticks = end_ticks - start_ticks;
	uint32_t duration_ms = (elapsed_ticks * 1000u) / pit_hz;

	char duration_buf[16];
	char read_buf[32];
	char expect_buf[32];
	u32_to_dec(duration_buf, sizeof(duration_buf), (unsigned long)duration_ms);
	u64_to_dec(read_buf, sizeof(read_buf), total_read);
	u64_to_dec(expect_buf, sizeof(expect_buf), expected);

	sysPrintf((int8_t *)"test_read: 耗时 ");
	sysPrintf((int8_t *)duration_buf);
	sysPrintf((int8_t *)"ms\n");
	sysPrintf((int8_t *)"test_read: 已读取 ");
	sysPrintf((int8_t *)read_buf);
	sysPrintf((int8_t *)" 字节（预期 ");
	sysPrintf((int8_t *)expect_buf);
	sysPrintf((int8_t *)" 字节）\n");


	vfs_close(fd);
}

static int build_churn_path(char *out, int out_sz, const char *dir, uint32_t idx)
{
	if (!out || out_sz <= 1 || !dir || dir[0] == '\0') {
		return -1;
	}

	int pos = 0;
	for (const char *p = dir; *p != '\0' && pos < out_sz - 1; ++p) {
		out[pos++] = *p;
	}
	if (pos == out_sz - 1) {
		out[pos] = '\0';
		return -1;
	}

	if (pos > 0 && out[pos - 1] != '/') {
		out[pos++] = '/';
		if (pos >= out_sz - 1) {
			out[out_sz - 1] = '\0';
			return -1;
		}
	}

	out[pos++] = 'f';
	out[pos++] = '_';

	char ibuf[16];
	u32_to_dec(ibuf, sizeof(ibuf), (unsigned long)idx);
	for (int i = 0; ibuf[i] != '\0' && pos < out_sz - 1; i++) {
		out[pos++] = ibuf[i];
	}
	if (pos >= out_sz - 1) {
		out[out_sz - 1] = '\0';
		return -1;
	}

	out[pos++] = '.';
	out[pos++] = 'd';
	out[pos++] = 'a';
	out[pos++] = 't';
	if (pos >= out_sz) {
		out[out_sz - 1] = '\0';
		return -1;
	}

	out[pos] = '\0';
	return 0;
}

static void cmd_test_churn(const int8_t *arg)
{
	if (!arg) {
		sysPrintf((int8_t *)"test_churn: 用法: test_churn <路径> <轮次> <每轮文件数> <文件大小>\n");
		return;
	}

	const char *p = (const char *)arg;
	char dir[128] = {0};
	char s_rounds[24] = {0};
	char s_files[24] = {0};
	char s_size[24] = {0};
	char s_extra[8] = {0};
	char *outs[5] = {dir, s_rounds, s_files, s_size, s_extra};
	int caps[5] = {(int)sizeof(dir), (int)sizeof(s_rounds), (int)sizeof(s_files), (int)sizeof(s_size), (int)sizeof(s_extra)};

	for (int t = 0; t < 5; t++) {
		int pos = 0;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		while (*p != '\0' && *p != ' ' && *p != '\t' && pos < caps[t] - 1) {
			outs[t][pos++] = *p++;
		}
		outs[t][pos] = '\0';
	}

	if (dir[0] == '\0' || s_rounds[0] == '\0' || s_files[0] == '\0' || s_size[0] == '\0' || s_extra[0] != '\0') {
		sysPrintf((int8_t *)"test_churn: 用法: test_churn DIR ROUNDS FILES_PER_ROUND FILE_SIZE\n");
		return;
	}

	uint64_t rounds64 = 0, files64 = 0, fsize64 = 0;
	if (parse_u64_10(s_rounds, &rounds64) != PARSE_U64_OK ||
	    parse_u64_10(s_files, &files64) != PARSE_U64_OK ||
	    parse_u64_10(s_size, &fsize64) != PARSE_U64_OK) {
		sysPrintf((int8_t *)"test_churn: 参数必须为十进制正整数\n");
		return;
	}

	if (rounds64 == 0 || files64 == 0) {
		sysPrintf((int8_t *)"test_churn: ROUNDS 和 FILES_PER_ROUND 必须大于 0\n");
		return;
	}
	if (rounds64 > 0xFFFFFFFFULL || files64 > 0xFFFFFFFFULL) {
		sysPrintf((int8_t *)"test_churn: ROUNDS/FILES_PER_ROUND 过大\n");
		return;
	}

	uint32_t rounds = (uint32_t)rounds64;
	uint32_t files_per_round = (uint32_t)files64;
	uint64_t file_size = fsize64;
	if (files_per_round > 1024U) {
		sysPrintf((int8_t *)"test_churn: FILES_PER_ROUND 过大（最大 1024）\n");
		return;
	}

	static char file_paths[1024][256];
	for (uint32_t i = 0; i < files_per_round; i++) {
		if (build_churn_path(file_paths[i], (int)sizeof(file_paths[i]), dir, i) != 0) {
			sysPrintf((int8_t *)"test_churn: 文件路径过长，请缩短目录名\n");
			return;
		}
	}

	static const int chunk_size = 4096;
	static char chunk[chunk_size];
	for (int i = 0; i < chunk_size; i++) {
		chunk[i] = (char)0x5A;
	}

	uint64_t created_ok = 0;
	uint64_t deleted_ok = 0;
	uint64_t create_fail = 0;
	uint64_t write_fail = 0;
	uint64_t delete_fail = 0;
	uint64_t delete_skip_missing = 0;
	uint64_t verify_fail = 0;
	int first_err = 0;

	uint32_t start_ticks = pit_get_ticks();
	uint32_t pit_hz = pit_get_frequency_hz();
	if (pit_hz == 0) {
		pit_hz = 1000;
	}

	for (uint32_t r = 0; r < rounds; r++) {
		for (uint32_t i = 0; i < files_per_round; i++) {
			const char *path = file_paths[i];
			int fd = vfs_open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
			if (fd < 0) {
				create_fail++;
				if (first_err == 0) {
					first_err = fd;
				}
				continue;
			}

			uint64_t remain = file_size;
			int write_ok = 1;
			while (remain > 0) {
				int to_write = (remain > (uint64_t)chunk_size) ? chunk_size : (int)remain;
				int w = vfs_write(fd, chunk, to_write);
				if (w <= 0) {
					write_fail++;
					write_ok = 0;
					if (first_err == 0) {
						first_err = (w < 0) ? w : -ENOSPC;
					}
					break;
				}
				remain -= (uint64_t)w;
			}

			vfs_close(fd);
			if (write_ok) {
				created_ok++;
			}
		}
		
		for (uint32_t i = 0; i < files_per_round; i++) {
			const char *path = file_paths[i];
			int uret = vfs_unlink((const int8_t *)path);
			if (uret == 0) {
				deleted_ok++;
			} else {
				if (uret == -ENOENT) {
					delete_skip_missing++;
				} else {
					delete_fail++;
					if (first_err == 0) {
						first_err = uret;
					}
				}
			}

			struct kstat st;
			int sret = vfs_stat(path, &st);
			if (sret != -ENOENT) {
				verify_fail++;
				if (first_err == 0) {
					first_err = (sret < 0) ? sret : -EEXIST;
				}
			}
		}
	}

	uint32_t end_ticks = pit_get_ticks();
	uint32_t elapsed_ticks = end_ticks - start_ticks;
	uint32_t duration_ms = (elapsed_ticks * 1000u) / pit_hz;

	uint64_t delete_errors = delete_fail + delete_skip_missing + verify_fail;
	char b_create[32], b_write[32], b_delete[32], b_ms[16];
	u64_to_dec(b_create, sizeof(b_create), create_fail);
	u64_to_dec(b_write, sizeof(b_write), write_fail);
	u64_to_dec(b_delete, sizeof(b_delete), delete_errors);
	u32_to_dec(b_ms, sizeof(b_ms), (unsigned long)duration_ms);

	sysPrintf((int8_t *)"test_churn: 完成\n");
	sysPrintf((int8_t *)"  create_err=");
	sysPrintf((int8_t *)b_create);
	sysPrintf((int8_t *)", write_err=");
	sysPrintf((int8_t *)b_write);
	sysPrintf((int8_t *)", delete_err=");
	sysPrintf((int8_t *)b_delete);
	sysPrintf((int8_t *)"\n");
	sysPrintf((int8_t *)"  耗时 ");
	sysPrintf((int8_t *)b_ms);
	sysPrintf((int8_t *)"ms\n");

	if (first_err != 0) {
		sysPrintf((int8_t *)"  first_err=");
		print_errno_value(first_err);
		sysPrintf((int8_t *)"\n");
	}
}

static void cmd_test_mkdir_deep(const int8_t *arg)
{
	if (!arg) {
		sysPrintf((int8_t *)"test_mkdir_deep: 用法: test_mkdir_deep <路径> <层数>\n");
		return;
	}

	const char *p = (const char *)arg;
	char base[128] = {0};
	char s_depth[24] = {0};
	char s_extra[8] = {0};
	char *outs[3] = {base, s_depth, s_extra};
	int caps[3] = {(int)sizeof(base), (int)sizeof(s_depth), (int)sizeof(s_extra)};

	for (int t = 0; t < 3; t++) {
		int pos = 0;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		while (*p != '\0' && *p != ' ' && *p != '\t' && pos < caps[t] - 1) {
			outs[t][pos++] = *p++;
		}
		outs[t][pos] = '\0';
	}

	if (base[0] == '\0' || s_depth[0] == '\0' || s_extra[0] != '\0') {
		sysPrintf((int8_t *)"test_mkdir_deep: 用法: test_mkdir_deep <路径> <层数>\n");
		return;
	}

	uint64_t depth64 = 0;
	if (parse_u64_10(s_depth, &depth64) != PARSE_U64_OK) {
		sysPrintf((int8_t *)"test_mkdir_deep: 层数必须是十进制正整数\n");
		return;
	}

	uint32_t depth = (uint32_t)depth64;

	char saved_cwd[256];
	int has_saved_cwd = 0;
	if (sysGetcwd(saved_cwd, sizeof(saved_cwd)) == 0) {
		has_saved_cwd = 1;
	}

	int cd_base = sysChdir((const int8_t *)base);
	if (cd_base != 0) {
		sysPrintf((int8_t *)"test_mkdir_deep: 无法进入基础路径，错误码=");
		print_errno_value(cd_base);
		sysPrintf((int8_t *)"\n");
		return;
	}

	uint64_t created = 0;
	uint64_t exists = 0;
	uint64_t failed = 0;
	int first_err = 0;

	uint32_t start_ticks = pit_get_ticks();
	uint32_t pit_hz = pit_get_frequency_hz();
	if (pit_hz == 0) {
		pit_hz = 1000;
	}

	for (uint32_t i = 0; i < depth; i++) {
		char ibuf[16];
		char name[32];
		u32_to_dec(ibuf, sizeof(ibuf), (unsigned long)i);
		int npos = 0;
		name[npos++] = 'd';
		name[npos++] = '_';
		for (int j = 0; ibuf[j] != '\0' && npos < (int)sizeof(name) - 1; j++) {
			name[npos++] = ibuf[j];
		}
		name[npos] = '\0';

		int ret = sysMkdir((const int8_t *)name);
		if (ret == 0) {
			created++;
		} else if (ret == -EEXIST) {
			exists++;
		} else {
			failed++;
			if (first_err == 0) {
				first_err = ret;
			}
			break;
		}

		int cdret = sysChdir((const int8_t *)name);
		if (cdret != 0) {
			failed++;
			if (first_err == 0) {
				first_err = cdret;
			}
			break;
		}
	}

	if (has_saved_cwd) {
		(void)sysChdir((const int8_t *)saved_cwd);
	}

	uint32_t end_ticks = pit_get_ticks();
	uint32_t elapsed_ticks = end_ticks - start_ticks;
	uint32_t duration_ms = (elapsed_ticks * 1000u) / pit_hz;

	char b0[32], b1[32], b2[32], b3[16];
	u64_to_dec(b0, sizeof(b0), created);
	u64_to_dec(b1, sizeof(b1), exists);
	u64_to_dec(b2, sizeof(b2), failed);
	u32_to_dec(b3, sizeof(b3), (unsigned long)duration_ms);

	sysPrintf((int8_t *)"test_mkdir_deep: 完成\n");
	sysPrintf((int8_t *)"  created=");
	sysPrintf((int8_t *)b0);
	sysPrintf((int8_t *)", exists=");
	sysPrintf((int8_t *)b1);
	sysPrintf((int8_t *)", failed=");
	sysPrintf((int8_t *)b2);
	sysPrintf((int8_t *)"\n");
	sysPrintf((int8_t *)"  耗时 ");
	sysPrintf((int8_t *)b3);
	sysPrintf((int8_t *)"ms\n");
	if (first_err != 0) {
		sysPrintf((int8_t *)"  first_err=");
		print_errno_value(first_err);
		sysPrintf((int8_t *)"\n");
	}
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

static const char *path_basename(const char *path)
{
	const char *last = path;
	if (!path || *path == '\0') {
		return path;
	}

	for (const char *p = path; *p != '\0'; ++p) {
		if (*p == '/') {
			last = p + 1;
		}
	}
	return last;
}

/* 简化 glob：支持 '*' 与 '?' */
static int match_glob(const char *pat, const char *text)
{
	if (!pat || !text) {
		return 0;
	}

	if (*pat == '\0') {
		return *text == '\0';
	}

	if (*pat == '*') {
		pat++;
		if (*pat == '\0') {
			return 1;
		}
		while (*text != '\0') {
			if (match_glob(pat, text)) {
				return 1;
			}
			text++;
		}
		return match_glob(pat, text);
	}

	if (*pat == '?') {
		if (*text == '\0') {
			return 0;
		}
		return match_glob(pat + 1, text + 1);
	}

	if (*pat != *text) {
		return 0;
	}
	return match_glob(pat + 1, text + 1);
}

static int should_print_find_path(const char *path, const char *name_pat)
{
	if (!name_pat || *name_pat == '\0') {
		return 1;
	}
	return match_glob(name_pat, path_basename(path));
}

static void find_print_child_path(const char *cwd, const char *name)
{
	char full[1024];
	int pos = 0;
	if (!cwd || !name) {
		return;
	}
	while (cwd[pos] != '\0' && pos < (int)sizeof(full) - 1) {
		full[pos] = cwd[pos];
		pos++;
	}
	if (pos > 0 && full[pos - 1] != '/' && pos < (int)sizeof(full) - 1) {
		full[pos++] = '/';
	}
	for (int i = 0; name[i] != '\0' && pos < (int)sizeof(full) - 1; i++) {
		full[pos++] = name[i];
	}
	full[pos] = '\0';
	sysPrintf((int8_t *)full);
	sysPrintf((int8_t *)"\n");
}

static void find_walk(const char *path, const char *name_pat)
{
	struct kstat st;
	int sret = sysStat(path, &st);
	if (sret < 0) {
		sysPrintf((int8_t *)"find: ");
		sysPrintf((int8_t *)path);
		sysPrintf((int8_t *)": 无法获取属性\n");
		return;
	}

	if (!S_ISDIR(st.mode)) {
		if (should_print_find_path(path, name_pat)) {
			sysPrintf((int8_t *)path);
			sysPrintf((int8_t *)"\n");
		}
		return;
	}

	char saved_cwd[1024];
	int has_saved = (sysGetcwd(saved_cwd, sizeof(saved_cwd)) == 0);
	if (sysChdir((const int8_t *)path) != 0) {
		sysPrintf((int8_t *)"find: ");
		sysPrintf((int8_t *)path);
		sysPrintf((int8_t *)": 无法进入目录\n");
		return;
	}

	char cwd_now[1024];
	if (sysGetcwd(cwd_now, sizeof(cwd_now)) == 0) {
		if (should_print_find_path(cwd_now, name_pat)) {
			sysPrintf((int8_t *)cwd_now);
			sysPrintf((int8_t *)"\n");
		}
	} else if (should_print_find_path(path, name_pat)) {
		sysPrintf((int8_t *)path);
		sysPrintf((int8_t *)"\n");
	}

	int fd = sysOpen(".", 0, 0);
	if (fd < 0) {
		sysPrintf((int8_t *)"find: 无法打开目录\n");
		if (has_saved) {
			(void)sysChdir((const int8_t *)saved_cwd);
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
			while (name[name_len] != '\0') {
				name_len++;
			}

			if (!((name_len == 1 && name[0] == '.') ||
			      (name_len == 2 && name[0] == '.' && name[1] == '.'))) {
				struct kstat child_st;
				int cs = sysStat(name, &child_st);
				if (cs == 0 && S_ISDIR(child_st.mode)) {
					find_walk(name, name_pat);
				} else if (cs == 0) {
					char cwd_file[1024];
					if (sysGetcwd(cwd_file, sizeof(cwd_file)) == 0) {
						if (should_print_find_path(name, name_pat)) {
							find_print_child_path(cwd_file, name);
						}
					} else if (should_print_find_path(name, name_pat)) {
						sysPrintf((int8_t *)name);
						sysPrintf((int8_t *)"\n");
					}
				}
			}

			bpos += d->d_reclen;
		}
	}
	sysClose(fd);

	if (has_saved) {
		(void)sysChdir((const int8_t *)saved_cwd);
	}
}

static void cmd_find(const int8_t *arg)
{
	const char *path = ".";
	const char *name_pat = 0;
	uint32_t start_ticks = pit_get_ticks();
	uint32_t pit_hz = pit_get_frequency_hz();
	if (pit_hz == 0) {
		pit_hz = 1000;
	}

	if (arg) {
		const char *p = (const char *)arg;
		const char *tok1 = 0;
		const char *tok2 = 0;
		const char *tok3 = 0;
		char a1[256] = {0};
		char a2[256] = {0};
		char a3[256] = {0};
		int *lens[3] = {0};

		int l1 = 0, l2 = 0, l3 = 0;
		lens[0] = &l1; lens[1] = &l2; lens[2] = &l3;

		char *outs[3] = {a1, a2, a3};
		for (int t = 0; t < 3; t++) {
			while (*p == ' ' || *p == '\t') {
				p++;
			}
			while (*p != '\0' && *p != ' ' && *p != '\t' && *lens[t] < 255) {
				outs[t][(*lens[t])++] = *p++;
			}
			outs[t][*lens[t]] = '\0';
			while (*p != '\0' && *p != ' ' && *p != '\t') {
				/* token 超长时，继续跳过剩余字符，避免误把尾巴当下一参数 */
				p++;
			}
		}

		tok1 = (l1 > 0) ? a1 : 0;
		tok2 = (l2 > 0) ? a2 : 0;
		tok3 = (l3 > 0) ? a3 : 0;

		if (tok1 && strcmp((const int8_t *)tok1, (const int8_t *)"-name") == 0) {
			if (!tok2) {
				sysPrintf((int8_t *)"find: 用法: find [PATH] [-name PATTERN]\n");
				return;
			}
			name_pat = tok2;
		} else {
			if (tok1) {
				path = tok1;
			}
			if (tok2) {
				if (strcmp((const int8_t *)tok2, (const int8_t *)"-name") != 0 || !tok3) {
					sysPrintf((int8_t *)"find: 用法: find [PATH] [-name PATTERN]\n");
					return;
				}
				name_pat = tok3;
			}
		}
	}

	find_walk(path, name_pat);

	uint32_t end_ticks = pit_get_ticks();
	uint32_t elapsed_ticks = end_ticks - start_ticks;
	uint32_t duration_ms = (elapsed_ticks * 1000u) / pit_hz;
	char duration_buf[16];
	u32_to_dec(duration_buf, sizeof(duration_buf), (unsigned long)duration_ms);
	sysPrintf((int8_t *)"find: 完成, 耗时 ");
	sysPrintf((int8_t *)duration_buf);
	sysPrintf((int8_t *)"ms\n");
}

static void cmd_help(const int8_t *arg);

const struct cmd_entry cmd_table[] = {
	{"help", cmd_help, "列出所有命令"},
	{"time", cmd_time, "显示当前 RTC 时间"},
	{"pwd", cmd_pwd, "显示当前工作目录"},
	{"ls", cmd_ls, "列出目录内容 [-l]详细 [-h]易读大小 [-i]显示inode"},
	{"mkdir", cmd_mkdir, "创建目录"},
	{"cd", cmd_cd, "切换工作目录"},
	{"touch", cmd_touch, "创建空文件（不存在时）"},
	{"echo", cmd_echo, "输出文本；支持 > 重定向到文件"},
	{"cat", cmd_cat, "显示文件内容"},
	{"find", cmd_find, "递归查找: find <路径> [-name <模式>]"},
	{"rm", cmd_rm, "删除文件"},
	{"rmdir", cmd_rmdir, "删除空目录"},
	{"ext4_mode", cmd_ext4_mode, "切换 ext4 调优模式: safe|perf"},
	{0, 0, 0},
	{"test_fill", cmd_test_fill, "性能测试: test_fill <路径> <字节数>"},
	{"test_read", cmd_test_read, "性能测试: test_read <路径>"},
	{"test_churn", cmd_test_churn, "抖动测试: test_churn <路径> <轮次> <每轮文件数> <文件大小>"},
	{"test_mkdir_deep", cmd_test_mkdir_deep, "目录测试: test_mkdir_deep <路径> <层数>"},
	{0, 0, 0},
};

static void cmd_help(const int8_t *arg)
{
	(void)arg;
	int index = 0;
	sysPrintf((int8_t *)"可用命令：\n");
	for (; cmd_table[index].name != 0; index++) {
		sysPrintf((int8_t *)"  ");
		sysPrintf((int8_t *)cmd_table[index].name);
		if (cmd_table[index].help) {
			sysPrintf((int8_t *)"\t- ");
			sysPrintf((int8_t *)cmd_table[index].help);
		}
		sysPrintf((int8_t *)"\n");
	}
	sysPrintf((int8_t *)"测试命令：\n");
	for (index += 1; cmd_table[index].name != 0; index++) {
		sysPrintf((int8_t *)"  ");
		sysPrintf((int8_t *)cmd_table[index].name);
		if (cmd_table[index].help) {
			sysPrintf((int8_t *)"\t- ");
			sysPrintf((int8_t *)cmd_table[index].help);
		}
		sysPrintf((int8_t *)"\n");
	}
}
