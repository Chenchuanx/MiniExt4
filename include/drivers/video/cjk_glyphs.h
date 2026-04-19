#ifndef MINIEXT4_DRIVERS_VIDEO_CJK_GLYPHS_H
#define MINIEXT4_DRIVERS_VIDEO_CJK_GLYPHS_H

#include <linux/types.h>

/*
 * 自嵌入 Unifont BMP 按码点采样，返回 16×16 点阵（16 行×2 字节/行，MSB 为左像素）。
 * 与终端格宽（如 fb_console 中 ASCII 8×16）无关；显示分辨率由调用方裁列/缩放决定。
 * 不可重入（内部静态缓冲）。
 */
const uint8_t *cjk_glyph_bitmap(uint32_t codepoint);

#endif
