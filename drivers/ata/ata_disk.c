/* ATA 磁盘向块层注册（对应 Linux drivers/ata 中 add_disk 路径） */
#include <linux/blkdev.h>
#include <linux/ata.h>

static struct block_device g_ata_bdev;

static int ata_bdev_read(struct block_device *bdev, uint64_t sector,
			  uint32_t count, void *buf)
{
	(void)bdev;
	return ata_read_sectors((uint32_t)sector, (uint8_t)count, buf);
}

static int ata_bdev_write(struct block_device *bdev, uint64_t sector,
			   uint32_t count, const void *buf)
{
	(void)bdev;
	return ata_write_sectors((uint32_t)sector, (uint8_t)count, buf);
}

static const struct block_device_ops ata_bdev_ops = {
	.read_sectors = ata_bdev_read,
	.write_sectors = ata_bdev_write,
};

int ata_disk_register(void)
{
	g_ata_bdev.bd_dev = 0x800;
	g_ata_bdev.bd_openers = 0;
	g_ata_bdev.bd_sector_size = ATA_SECTOR_SIZE;
	g_ata_bdev.bd_nr_sectors = ata_get_total_sectors();
	g_ata_bdev.bd_ops = &ata_bdev_ops;
	g_ata_bdev.bd_super = 0;
	g_ata_bdev.bd_private = 0;

	return blkdev_register(&g_ata_bdev);
}
