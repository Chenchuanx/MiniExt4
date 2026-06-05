/* 块设备子系统（对应 Linux block/ + include/linux/blkdev.h） */
#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct super_block;
struct block_device;

struct block_device_ops {
	int (*read_sectors)(struct block_device *bdev,
			    uint64_t sector, uint32_t count, void *buf);
	int (*write_sectors)(struct block_device *bdev,
			     uint64_t sector, uint32_t count, const void *buf);
};

struct block_device {
	dev_t			bd_dev;
	int			bd_openers;
	unsigned int		bd_sector_size;
	uint64_t		bd_nr_sectors;
	const struct block_device_ops *bd_ops;
	struct super_block	*bd_super;
	void			*bd_private;
};

int blkdev_register(struct block_device *bdev);
struct block_device *blkdev_get_by_name(const char *name);
struct block_device *blkdev_get_default(void);
int blkdev_read_sectors(struct block_device *bdev,
			uint64_t sector, uint32_t count, void *buf);
int blkdev_write_sectors(struct block_device *bdev,
			 uint64_t sector, uint32_t count, const void *buf);

static inline uint64_t blkdev_nr_sectors(struct block_device *bdev)
{
	return bdev ? bdev->bd_nr_sectors : 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _LINUX_BLKDEV_H */
