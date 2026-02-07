/* 
 * ATA/IDE 驱动实现
 * 支持 PIO 模式读写
 */
#include <drivers/ata.h>
#include <lib/port.h>

/* 等待驱动器就绪（清除 BSY 位） */
static bool ata_wait_bsy(void)
{
    Port8Bit status_port(ATA_STATUS);
    uint8_t status;
    
    /* 最多等待 30 秒（简化版，实际应该用超时） */
    for (int i = 0; i < 30000; i++) {
        status = status_port.Read();
        if (!(status & ATA_SR_BSY)) {
            return true;
        }
        /* 简单延时（实际应该用更精确的延时） */
        for (volatile int j = 0; j < 1000; j++);
    }
    return false;
}

/* 等待数据就绪（DRQ 位） */
static bool ata_wait_drq(void)
{
    Port8Bit status_port(ATA_STATUS);
    uint8_t status;
    
    for (int i = 0; i < 30000; i++) {
        status = status_port.Read();
        if (status & ATA_SR_DRQ) {
            return true;
        }
        if (status & ATA_SR_ERR) {
            return false;
        }
        for (volatile int j = 0; j < 1000; j++);
    }
    return false;
}

/**
 * ata_init - 初始化 ATA 驱动
 */
int ata_init(void)
{
    Port8Bit control_port(ATA_CONTROL);
    Port8Bit status_port(ATA_STATUS);
    
    /* 重置控制器 */
    control_port.Write(0x04);  /* SRST */
    for (volatile int i = 0; i < 10000; i++);
    control_port.Write(0x00);
    for (volatile int i = 0; i < 10000; i++);
    
    /* 等待驱动器就绪 */
    if (!ata_wait_bsy()) {
        return -1;
    }
    
    /* 检查是否有设备（简化版，实际应该用 IDENTIFY） */
    uint8_t status = status_port.Read();
    if (status == 0xFF) {
        /* 没有设备 */
        return -1;
    }
    
    return 0;
}

/**
 * ata_read_sectors - 从硬盘读取扇区（LBA28）
 */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf)
{
    Port8Bit device_port(ATA_DEVICE);
    Port8Bit sector_count_port(ATA_SECTOR_COUNT);
    Port8Bit lba_low_port(ATA_LBA_LOW);
    Port8Bit lba_mid_port(ATA_LBA_MID);
    Port8Bit lba_high_port(ATA_LBA_HIGH);
    Port8Bit command_port(ATA_COMMAND);
    Port16Bit data_port(ATA_DATA);
    
    if (!buf || count == 0) {
        return -1;
    }
    
    /* 等待驱动器就绪 */
    if (!ata_wait_bsy()) {
        return -1;
    }
    
    /* 选择主盘（0xE0）并设置 LBA 模式 */
    device_port.Write(0xE0 | ((lba >> 24) & 0x0F));
    
    /* 设置扇区数 */
    sector_count_port.Write(count);
    
    /* 设置 LBA 地址（低 24 位） */
    lba_low_port.Write((uint8_t)(lba & 0xFF));
    lba_mid_port.Write((uint8_t)((lba >> 8) & 0xFF));
    lba_high_port.Write((uint8_t)((lba >> 16) & 0xFF));
    
    /* 发送读取命令 */
    command_port.Write(ATA_CMD_READ_SECTORS);
    
    /* 读取数据 */
    uint16_t *buffer = (uint16_t *)buf;
    for (int s = 0; s < (int)count; s++) {
        /* 等待数据就绪 */
        if (!ata_wait_drq()) {
            return -1;
        }
        
        /* 读取一个扇区（256 个 16 位字 = 512 字节） */
        for (int i = 0; i < 256; i++) {
            buffer[i] = data_port.Read();
        }
        
        buffer += 256;
    }
    
    return 0;
}

/**
 * ata_write_sectors - 向硬盘写入扇区（LBA28）
 */
int ata_write_sectors(uint32_t lba, uint8_t count, const void *buf)
{
    Port8Bit device_port(ATA_DEVICE);
    Port8Bit sector_count_port(ATA_SECTOR_COUNT);
    Port8Bit lba_low_port(ATA_LBA_LOW);
    Port8Bit lba_mid_port(ATA_LBA_MID);
    Port8Bit lba_high_port(ATA_LBA_HIGH);
    Port8Bit command_port(ATA_COMMAND);
    Port16Bit data_port(ATA_DATA);
    
    if (!buf || count == 0) {
        return -1;
    }
    
    /* 等待驱动器就绪 */
    if (!ata_wait_bsy()) {
        return -1;
    }
    
    /* 选择主盘并设置 LBA 模式 */
    device_port.Write(0xE0 | ((lba >> 24) & 0x0F));
    
    /* 设置扇区数 */
    sector_count_port.Write(count);
    
    /* 设置 LBA 地址 */
    lba_low_port.Write((uint8_t)(lba & 0xFF));
    lba_mid_port.Write((uint8_t)((lba >> 8) & 0xFF));
    lba_high_port.Write((uint8_t)((lba >> 16) & 0xFF));
    
    /* 发送写入命令 */
    command_port.Write(ATA_CMD_WRITE_SECTORS);
    
    /* 写入数据 */
    const uint16_t *buffer = (const uint16_t *)buf;
    for (int s = 0; s < (int)count; s++) {
        /* 等待数据请求 */
        if (!ata_wait_drq()) {
            return -1;
        }
        
        /* 写入一个扇区 */
        for (int i = 0; i < 256; i++) {
            data_port.Write(buffer[i]);
        }
        
        buffer += 256;
        
        /* 等待写入完成 */
        if (!ata_wait_bsy()) {
            return -1;
        }
    }
    
    return 0;
}

