/* 
 * Ext4 块 I/O 实现
 * 封装 ATA 驱动，提供按块读写接口
 */
#include <fs/ext4/ext4.h>
#include <drivers/ata.h>

/* 当前块大小（从 superblock 读取后设置） */
static uint32_t ext4_block_size = 4096;  /* 默认 4KB */

/**
 * ext4_set_block_size - 设置块大小
 * @size: 块大小（字节）
 */
void ext4_set_block_size(uint32_t size)
{
	ext4_block_size = size;
}

/**
 * ext4_get_block_size - 获取块大小
 */
uint32_t ext4_get_block_size(void)
{
	return ext4_block_size;
}

/**
 * ext4_read_block - 读取一个块
 * @blocknr: 块号
 * @buf: 缓冲区（必须至少 block_size 字节）
 * 
 * 返回 0 表示成功，负数表示失败
 */
int ext4_read_block(uint32_t blocknr, void *buf)
{
	if (!buf) {
		return -1;
	}
	
	/* 计算需要读取的扇区数 */
	uint32_t sectors_per_block = ext4_block_size / ATA_SECTOR_SIZE;
	if (sectors_per_block == 0) {
		sectors_per_block = 1;
	}
	
	/* 计算起始 LBA（假设从 LBA 0 开始，实际应该考虑分区偏移） */
	uint32_t lba = blocknr * sectors_per_block;
	
	/* 调用 ATA 驱动读取 */
	return ata_read_sectors(lba, (uint8_t)sectors_per_block, buf);
}

/**
 * ext4_write_block - 写入一个块
 * @blocknr: 块号
 * @buf: 数据缓冲区（必须至少 block_size 字节）
 * 
 * 返回 0 表示成功，负数表示失败
 */
int ext4_write_block(uint32_t blocknr, const void *buf)
{
	if (!buf) {
		return -1;
	}
	
	uint32_t sectors_per_block = ext4_block_size / ATA_SECTOR_SIZE;
	if (sectors_per_block == 0) {
		sectors_per_block = 1;
	}
	
	uint32_t lba = blocknr * sectors_per_block;
	
	return ata_write_sectors(lba, (uint8_t)sectors_per_block, buf);
}

int ext4_write_blocks(uint32_t blocknr, uint32_t blocks, const void *buf)
{
	uint32_t sectors_per_block;
	uint32_t lba;
	uint32_t total_sectors;
	const uint8_t *p;

	if (!buf || blocks == 0) {
		return -1;
	}

	sectors_per_block = ext4_block_size / ATA_SECTOR_SIZE;
	if (sectors_per_block == 0) {
		sectors_per_block = 1;
	}
	lba = blocknr * sectors_per_block;
	total_sectors = blocks * sectors_per_block;
	p = (const uint8_t *)buf;

	/* ATA 扇区计数是 8 bit，每次最多 255 扇区，分批提交。 */
	while (total_sectors > 0) {
		uint8_t chunk = (total_sectors > 255U) ? 255U : (uint8_t)total_sectors;
		if (ata_write_sectors(lba, chunk, p) < 0) {
			return -1;
		}
		lba += chunk;
		p += (uint32_t)chunk * ATA_SECTOR_SIZE;
		total_sectors -= chunk;
	}

	return 0;
}

