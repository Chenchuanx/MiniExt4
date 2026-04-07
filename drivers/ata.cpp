/* 
 * ATA/IDE 驱动实现
 * 支持 PIO 模式读写
 */
#include <drivers/ata.h>
#include <lib/port.h>

/* ATA DMA 命令 */
#define ATA_CMD_READ_DMA      0xC8
#define ATA_CMD_WRITE_DMA     0xCA

/* PCI 配置空间（type 1 访问，单总线足够） */
#define PCI_CONFIG_ADDR       0xCF8
#define PCI_CONFIG_DATA       0xCFC

/* Bus Master IDE 主通道寄存器偏移 */
#define BM_CMD_OFF            0x00
#define BM_STATUS_OFF         0x02
#define BM_PRDT_OFF           0x04

/* BM 命令位 */
#define BM_CMD_START          0x01
#define BM_CMD_READ_FROM_DISK 0x08 /* 1=内存<-磁盘(读), 0=内存->磁盘(写) */

/* BM 状态位 */
#define BM_ST_ACTIVE          0x01
#define BM_ST_ERROR           0x02
#define BM_ST_IRQ             0x04

/* PRDT 单项描述符 */
struct ata_prd_entry {
    uint32_t base_addr;
    uint16_t byte_count; /* 0 表示 64KB */
    uint16_t flags;      /* bit15=EOT */
} __attribute__((packed));

static ata_prd_entry g_ata_prdt __attribute__((aligned(16)));
static bool g_ata_dma_available = false;
/* 由 PCI BAR4 得到的主通道 Bus Master IDE I/O 基址（0 表示未探测到） */
static uint16_t g_ata_bmide_base = 0;

static uint32_t pci_read_config32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset)
{
	Port32Bit addr(PCI_CONFIG_ADDR);
	Port32Bit data(PCI_CONFIG_DATA);
	uint32_t a = 0x80000000U
		| ((uint32_t)bus << 16)
		| ((uint32_t)dev << 11)
		| ((uint32_t)func << 8)
		| ((uint32_t)offset & 0xFCU);
	addr.Write(a);
	return data.Read();
}

static void pci_write_config32(uint8_t bus, uint8_t dev, uint8_t func,
			       uint8_t offset, uint32_t value)
{
	Port32Bit addr(PCI_CONFIG_ADDR);
	Port32Bit data(PCI_CONFIG_DATA);
	uint32_t a = 0x80000000U
		| ((uint32_t)bus << 16)
		| ((uint32_t)dev << 11)
		| ((uint32_t)func << 8)
		| ((uint32_t)offset & 0xFCU);
	addr.Write(a);
	data.Write(value);
}

/*
 * 在 PCI 总线上查找 class=01/01（IDE）设备，读 BAR4 作为 BMIDE 端口基址，
 * 并置位 PCI Command 的 Bus Master + I/O Space。
 */
static uint16_t ata_pci_probe_bmide_primary(void)
{
	for (uint8_t d = 0; d < 32; d++) {
		uint32_t id0 = pci_read_config32(0, d, 0, 0);
		if ((id0 & 0xFFFFU) == 0xFFFFU)
			continue;

		uint32_t hdr_dw = pci_read_config32(0, d, 0, 0x0CU);
		uint8_t hdr_type = (uint8_t)((hdr_dw >> 16) & 0xFFU);
		int max_func = (hdr_type & 0x80U) ? 8 : 1;

		for (int f = 0; f < max_func; f++) {
			uint32_t id = pci_read_config32(0, d, (uint8_t)f, 0);
			if ((id & 0xFFFFU) == 0xFFFFU)
				continue;

			uint32_t class_rev = pci_read_config32(0, d, (uint8_t)f, 0x08U);
			uint8_t base_class = (uint8_t)((class_rev >> 24) & 0xFFU);
			uint8_t sub_class = (uint8_t)((class_rev >> 16) & 0xFFU);
			if (base_class != 0x01U || sub_class != 0x01U)
				continue;

			uint32_t bar4 = pci_read_config32(0, d, (uint8_t)f, 0x20U);
			if ((bar4 & 1U) == 0)
				continue;
			uint16_t io_base = (uint16_t)(bar4 & 0xFFFCU);
			if (io_base == 0)
				continue;

			uint32_t cmdstat = pci_read_config32(0, d, (uint8_t)f, 0x04U);
			uint16_t cmd = (uint16_t)(cmdstat & 0xFFFFU);
			cmd = (uint16_t)(cmd | 0x0005U); /* I/O space + Bus Master */
			pci_write_config32(0, d, (uint8_t)f, 0x04U,
					   (cmdstat & 0xFFFF0000U) | cmd);

			return io_base;
		}
	}
	return 0;
}

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

/* 轮询 DMA 结束 */
static bool ata_wait_dma_done(void)
{
    Port8Bit bm_status((uint16_t)(g_ata_bmide_base + BM_STATUS_OFF));
    for (int i = 0; i < 30000; i++) {
        uint8_t st = bm_status.Read();
        if (!(st & BM_ST_ACTIVE)) {
            if (st & BM_ST_ERROR) {
                return false;
            }
            return true;
        }
        for (volatile int j = 0; j < 1000; j++);
    }
    return false;
}

static int ata_dma_rw(uint32_t lba, uint8_t count, void *buf, bool is_read)
{
    if (!g_ata_dma_available || g_ata_bmide_base == 0 || !buf || count == 0) {
        return -1;
    }

    /* 单 PRD 限制：最多 64KB，超出时回退 PIO */
    uint32_t total_bytes = (uint32_t)count * ATA_SECTOR_SIZE;
    if (total_bytes > 65536U) {
        return -1;
    }

    Port8Bit device_port(ATA_DEVICE);
    Port8Bit sector_count_port(ATA_SECTOR_COUNT);
    Port8Bit lba_low_port(ATA_LBA_LOW);
    Port8Bit lba_mid_port(ATA_LBA_MID);
    Port8Bit lba_high_port(ATA_LBA_HIGH);
    Port8Bit command_port(ATA_COMMAND);

    Port8Bit bm_cmd((uint16_t)(g_ata_bmide_base + BM_CMD_OFF));
    Port8Bit bm_status((uint16_t)(g_ata_bmide_base + BM_STATUS_OFF));
    Port32Bit bm_prdt((uint16_t)(g_ata_bmide_base + BM_PRDT_OFF));

    if (!ata_wait_bsy()) {
        return -1;
    }

    /* 配置 PRDT（当前内核平坦映射，虚拟地址可直接作物理地址） */
    g_ata_prdt.base_addr = (uint32_t)(uintptr_t)buf;
    g_ata_prdt.byte_count = (total_bytes == 65536U) ? 0 : (uint16_t)total_bytes;
    g_ata_prdt.flags = 0x8000; /* EOT */

    /* 停止 BM 并清中断/错误位 */
    bm_cmd.Write(0x00);
    bm_status.Write(BM_ST_IRQ | BM_ST_ERROR);
    bm_prdt.Write((uint32_t)(uintptr_t)&g_ata_prdt);

    /* 方向位：读时置位，写时清零 */
    uint8_t cmdv = is_read ? BM_CMD_READ_FROM_DISK : 0x00;
    bm_cmd.Write(cmdv);

    /* 配置 ATA 任务文件寄存器 */
    device_port.Write(0xE0 | ((lba >> 24) & 0x0F));
    sector_count_port.Write(count);
    lba_low_port.Write((uint8_t)(lba & 0xFF));
    lba_mid_port.Write((uint8_t)((lba >> 8) & 0xFF));
    lba_high_port.Write((uint8_t)((lba >> 16) & 0xFF));

    command_port.Write(is_read ? ATA_CMD_READ_DMA : ATA_CMD_WRITE_DMA);

    /* 启动 DMA */
    bm_cmd.Write((uint8_t)(cmdv | BM_CMD_START));

    if (!ata_wait_dma_done()) {
        bm_cmd.Write(cmdv); /* 停止 */
        return -1;
    }

    /* 停止 DMA 引擎 */
    bm_cmd.Write(cmdv);

    /* ATA 侧错误检查 */
    {
        Port8Bit status_port(ATA_STATUS);
        uint8_t st = status_port.Read();
        if (st & ATA_SR_ERR) {
            return -1;
        }
    }
    return 0;
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

    g_ata_bmide_base = ata_pci_probe_bmide_primary();
    g_ata_dma_available = (g_ata_bmide_base != 0);

    return 0;
}

/**
 * ata_read_sectors - 从硬盘读取扇区（LBA28）
 */
int ata_read_sectors(uint32_t lba, uint8_t count, void *buf)
{
    if (ata_dma_rw(lba, count, buf, true) == 0) {
        return 0;
    }

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
    if (ata_dma_rw(lba, count, (void *)buf, false) == 0) {
        return 0;
    }

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

/**
 * ata_get_total_sectors - 通过 IDENTIFY 获取磁盘总扇区数（LBA28）
 */
uint32_t ata_get_total_sectors(void)
{
    Port8Bit device_port(ATA_DEVICE);
    Port8Bit sector_count_port(ATA_SECTOR_COUNT);
    Port8Bit lba_low_port(ATA_LBA_LOW);
    Port8Bit lba_mid_port(ATA_LBA_MID);
    Port8Bit lba_high_port(ATA_LBA_HIGH);
    Port8Bit command_port(ATA_COMMAND);
    Port16Bit data_port(ATA_DATA);
    Port8Bit status_port(ATA_STATUS);
    uint16_t identify[256];
    uint8_t status;

    if (!ata_wait_bsy()) {
        return 0;
    }

    /* 选主盘，清空任务寄存器后发送 IDENTIFY */
    device_port.Write(0xA0);
    sector_count_port.Write(0);
    lba_low_port.Write(0);
    lba_mid_port.Write(0);
    lba_high_port.Write(0);
    command_port.Write(ATA_CMD_IDENTIFY);

    status = status_port.Read();
    if (status == 0) {
        return 0;
    }
    if (!ata_wait_drq()) {
        return 0;
    }

    for (int i = 0; i < 256; i++) {
        identify[i] = data_port.Read();
    }

    /* IDENTIFY words 60-61: LBA28 总扇区数（低/高 16 位） */
    return ((uint32_t)identify[61] << 16) | (uint32_t)identify[60];
}

