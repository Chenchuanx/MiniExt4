/* 
 * ATA/IDE 驱动头文件
 * 支持 PIO 模式读写
 */
#ifndef _DRIVERS_ATA_H
#define _DRIVERS_ATA_H

#include <linux/types.h>

/* ATA 主通道端口 */
#define ATA_DATA        0x1F0
#define ATA_ERROR       0x1F1
#define ATA_FEATURES    0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW     0x1F3
#define ATA_LBA_MID     0x1F4
#define ATA_LBA_HIGH    0x1F5
#define ATA_DEVICE      0x1F6
#define ATA_COMMAND     0x1F7
#define ATA_STATUS      0x1F7
#define ATA_CONTROL     0x3F6

/* ATA 状态寄存器位 */
#define ATA_SR_BSY      0x80    /* 忙 */
#define ATA_SR_DRDY     0x40    /* 就绪 */
#define ATA_SR_DF       0x20    /* 驱动器故障 */
#define ATA_SR_DSC      0x10    /* 寻道完成 */
#define ATA_SR_DRQ      0x08    /* 数据请求 */
#define ATA_SR_CORR     0x04    /* 已纠正 */
#define ATA_SR_IDX      0x02    /* 索引 */
#define ATA_SR_ERR      0x01    /* 错误 */

/* ATA 命令 */
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_IDENTIFY        0xEC

/* 扇区大小（字节） */
#define ATA_SECTOR_SIZE 512

/**
 * ata_init - 初始化 ATA 驱动
 * 
 * 检测 IDE 主通道是否有设备
 * 返回 0 表示成功，负数表示失败
 */
int ata_init(void);

/**
 * ata_read_sectors - 从硬盘读取扇区
 * @lba: 逻辑块地址（LBA）
 * @count: 要读取的扇区数
 * @buf: 缓冲区（必须至少 count * 512 字节）
 * 
 * 返回 0 表示成功，负数表示失败
 */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf);

/**
 * ata_write_sectors - 向硬盘写入扇区
 * @lba: 逻辑块地址（LBA）
 * @count: 要写入的扇区数
 * @buf: 数据缓冲区（必须至少 count * 512 字节）
 * 
 * 返回 0 表示成功，负数表示失败
 */
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);

#endif /* _DRIVERS_ATA_H */

