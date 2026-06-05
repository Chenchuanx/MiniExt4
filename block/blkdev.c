/* 块设备子系统核心（对应 Linux block/blk-core.c 等） */
#include <linux/blkdev.h>

static struct block_device *g_default_bdev;

static int blkdev_name_match(const char *name)
{
	if (!name || name[0] == '\0') {
		return 1;
	}
	if (name[0] == '0' && name[1] == '\0') {
		return 1;
	}
	if (name[0] == 'h' && name[1] == 'd' && name[2] == 'a' && name[3] == '\0') {
		return 1;
	}
	return 0;
}

int blkdev_register(struct block_device *bdev)
{
	if (!bdev || !bdev->bd_ops) {
		return -1;
	}
	g_default_bdev = bdev;
	return 0;
}

struct block_device *blkdev_get_default(void)
{
	return g_default_bdev;
}

struct block_device *blkdev_get_by_name(const char *name)
{
	if (!g_default_bdev) {
		return 0;
	}
	if (!blkdev_name_match(name)) {
		return 0;
	}
	return g_default_bdev;
}

int blkdev_read_sectors(struct block_device *bdev,
			uint64_t sector, uint32_t count, void *buf)
{
	uint8_t *p = (uint8_t *)buf;
	uint32_t remaining = count;
	uint32_t sector_size;

	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->read_sectors || !buf || count == 0) {
		return -1;
	}

	sector_size = bdev->bd_sector_size;
	while (remaining > 0) {
		uint8_t chunk = (remaining > 255U) ? 255U : (uint8_t)remaining;

		if (bdev->bd_ops->read_sectors(bdev, sector, chunk, p) < 0) {
			return -1;
		}
		sector += chunk;
		p += (uint32_t)chunk * sector_size;
		remaining -= chunk;
	}

	return 0;
}

int blkdev_write_sectors(struct block_device *bdev,
			 uint64_t sector, uint32_t count, const void *buf)
{
	const uint8_t *p = (const uint8_t *)buf;
	uint32_t remaining = count;
	uint32_t sector_size;

	if (!bdev || !bdev->bd_ops || !bdev->bd_ops->write_sectors || !buf || count == 0) {
		return -1;
	}

	sector_size = bdev->bd_sector_size;
	while (remaining > 0) {
		uint8_t chunk = (remaining > 255U) ? 255U : (uint8_t)remaining;

		if (bdev->bd_ops->write_sectors(bdev, sector, chunk, p) < 0) {
			return -1;
		}
		sector += chunk;
		p += (uint32_t)chunk * sector_size;
		remaining -= chunk;
	}

	return 0;
}
