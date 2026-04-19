#ifndef MINIEXT4_DRIVERS_VIDEO_FB_CONSOLE_H
#define MINIEXT4_DRIVERS_VIDEO_FB_CONSOLE_H

#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Multiboot 线性帧缓冲上的 UTF-8 文本（含内嵌汉字子集），供 GTK 窗口显示。
 * 须在首次 printf 前调用 fb_console_init；失败时 printf 仍走 VGA 文本。
 */
void fb_console_init(const void *multiboot_info, uint32_t multiboot_magic);
bool fb_console_active(void);
void fb_console_puts(const int8_t *utf8);

#ifdef __cplusplus
}
#endif

#endif
