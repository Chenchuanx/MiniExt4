/* ATA/IDE 驱动（对应 Linux drivers/ata/） */
#ifndef _LINUX_ATA_H
#define _LINUX_ATA_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ATA_DATA         0x1F0
#define ATA_ERROR        0x1F1
#define ATA_FEATURES     0x1F1
#define ATA_SECTOR_COUNT 0x1F2
#define ATA_LBA_LOW      0x1F3
#define ATA_LBA_MID      0x1F4
#define ATA_LBA_HIGH     0x1F5
#define ATA_DEVICE       0x1F6
#define ATA_COMMAND      0x1F7
#define ATA_STATUS       0x1F7
#define ATA_CONTROL      0x3F6

#define ATA_SR_BSY       0x80
#define ATA_SR_DRDY      0x40
#define ATA_SR_DF        0x20
#define ATA_SR_DSC       0x10
#define ATA_SR_DRQ       0x08
#define ATA_SR_CORR      0x04
#define ATA_SR_IDX       0x02
#define ATA_SR_ERR       0x01

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_IDENTIFY      0xEC

#define ATA_SECTOR_SIZE  512

int ata_init(void);
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf);
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf);
uint32_t ata_get_total_sectors(void);

/* 向块层注册 ATA 磁盘（对应 Linux add_disk） */
int ata_disk_register(void);

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_ATA_H */
