#ifndef MINIEXT4_DRIVERS_VIDEO_CJK_GLYPHS_H
#define MINIEXT4_DRIVERS_VIDEO_CJK_GLYPHS_H

#include <linux/types.h>

/* Unifont BMP 内嵌字形，16 行 × 2 字节/行，MSB 为左像素；不可重入（静态缓冲）。 */
const uint8_t *cjk_glyph_bitmap(uint32_t codepoint);

#endif
