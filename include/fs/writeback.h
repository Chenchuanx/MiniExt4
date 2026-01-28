/* 回写控制结构，适用于 MiniExt4 项目 */
#ifndef _LINUX_FS_WRITEBACK_H
#define _LINUX_FS_WRITEBACK_H

#include <linux/types.h>

/* 前向声明 */
struct inode;

/**
 * writeback_control - 回写控制结构
 * 
 * 用于控制 inode 数据写回磁盘的过程
 */
struct writeback_control {
	/* 回写原因 */
	long nr_to_write;		/* 要写入的页数 */
	long pages_skipped;		/* 跳过的页数 */
	
	/* 控制标志 */
	unsigned int sync_mode;		/* 同步模式 */
	unsigned int for_kupdate:1;	/* 内核更新 */
	unsigned int for_background:1;	/* 后台回写 */
	unsigned int for_sync:1;		/* 同步回写 */
	unsigned int range_cyclic:1;	/* 循环范围 */
	unsigned int range_start;	/* 起始范围 */
	unsigned int range_end;		/* 结束范围 */
	
	/* 更多控制 */
	unsigned int more_io:1;		/* 更多 I/O */
	unsigned int encountered_congestion:1;	/* 遇到拥塞 */
};

/* 同步模式 */
#define WB_SYNC_NONE	0	/* 异步回写 */
#define WB_SYNC_ALL	1	/* 同步回写所有数据 */

#endif /* _LINUX_FS_WRITEBACK_H */

