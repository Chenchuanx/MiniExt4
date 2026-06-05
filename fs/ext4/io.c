/* 
 * Ext4 块 I/O 实现
 * 通过块设备层按文件系统块读写
 */
#include <fs/ext4/ext4.h>
#include <linux/blkdev.h>

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

// 获取块设备
static struct block_device *ext4_io_bdev(void)
{
	return blkdev_get_default();
}

static uint32_t ext4_sectors_per_block(struct block_device *bdev)
{
	uint32_t spp = ext4_block_size / bdev->bd_sector_size;

	return spp ? spp : 1;
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

	struct block_device *bdev = ext4_io_bdev(); 
	if (!bdev) {
		return -1;
	}

	uint32_t  sectors_per_block = ext4_sectors_per_block(bdev);	// 每个块的扇区数
	uint64_t lba = (uint64_t)blocknr * sectors_per_block;	// 扇区号

	return blkdev_read_sectors(bdev, lba, sectors_per_block, buf); // 读取扇区
}

int ext4_read_blocks(uint32_t blocknr, uint32_t blocks, void *buf)
{
	struct block_device *bdev;
	uint32_t sectors_per_block;
	uint64_t lba;
	uint32_t total_sectors;

	if (!buf || blocks == 0) {
		return -1;
	}

	bdev = ext4_io_bdev();
	if (!bdev) {
		return -1;
	}

	sectors_per_block = ext4_sectors_per_block(bdev);
	lba = (uint64_t)blocknr * sectors_per_block;
	total_sectors = blocks * sectors_per_block;

	return blkdev_read_sectors(bdev, lba, total_sectors, buf);
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
	struct block_device *bdev;
	uint32_t sectors_per_block;
	uint64_t lba;

	if (!buf) {
		return -1;
	}

	bdev = ext4_io_bdev();
	if (!bdev) {
		return -1;
	}

	sectors_per_block = ext4_sectors_per_block(bdev);
	lba = (uint64_t)blocknr * sectors_per_block;

	return blkdev_write_sectors(bdev, lba, sectors_per_block, buf);
}

int ext4_write_blocks(uint32_t blocknr, uint32_t blocks, const void *buf)
{
	struct block_device *bdev;
	uint32_t sectors_per_block;
	uint64_t lba;
	uint32_t total_sectors;

	if (!buf || blocks == 0) {
		return -1;
	}

	bdev = ext4_io_bdev();
	if (!bdev) {
		return -1;
	}

	sectors_per_block = ext4_sectors_per_block(bdev);
	lba = (uint64_t)blocknr * sectors_per_block;
	total_sectors = blocks * sectors_per_block;

	return blkdev_write_sectors(bdev, lba, total_sectors, buf);
}
