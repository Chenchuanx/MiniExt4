/*
 * BMP 0 平面 Unifont（16×16）：Unicode 直接定位字形，无需 GB2312 表。
 * 字库链接时 objcopy 嵌入（_binary_unifont_bmp_*）。BMP 0 平面 U+0000–U+FFFF；其余返回 NULL。
 *
 * 中文 / CJK：与英文一样都是 BMP 里的码点，没有单独的「中文读取方式」。官方全平面图由
 * unifontpic(1) 自 .hex 生成，默认 256×256 格、每格 16×16，码点 U+HHLL 对应第 HH 行、第 LL 列
 *（HH=cp>>8，LL=cp&0xFF）。参阅 Unifont 工具说明 https://unifoundry.com/unifont/unifont-utilities.html
 * 及发行镜像 https://github.com/multitheftauto/unifont/（README 主要为 TTF 镜像与许可证，排布以
 * unifoundry 工具链与 .hex/BMP 为准）。
 *
 * === 图幅排布（与全图 BMP 一致；四周有边框/图例）===
 * 视觉坐标：原点＝整幅图左上角。字格固定 16×16；格线可能使格点不落在整图 16 倍数上，
 * 必须以锚点实测左上坐标为准（勿假定左右对称 16px）。
 *
 * 定位方式：以表位「0041」= U+0041 的 16×16 字格左上角为锚（Python 在全图上核对：该格左上
 * (1071,63)，字内墨迹约 (1071,63)–(1086,78)，与用户标注一致）。由此：
 *   origin_x = 1071 - 0x41*16 = 31（左留白 31px；宽 4128 时右缘余量约 1px，边距不对称）
 *   U+00xx 行顶边 origin_y_row0 = 63
 *
 * 一般码点：x0 = origin_x + (cp & 0xFF) * 16。纵向上每行对应 Unicode 高字节 HH，数据行号 row = HH。
 * y0 = origin_y_row0 + row * 16 + kBmpGlyphSampleYOffsetPx：格线与 16 网格略有偏差时整体下移取样
 *（勿过大：图高 4160 时 +1 对底行仍有余量；再大易 y0+16>h 取字失败）。
 *
 * === 与排布独立的：Windows 1bpp DIB 行序 ===
 * biHeight > 0 时为首行在文件末尾的 bottom-up；以「视觉 y」取像素须 yb = h - 1 - y。
 * biHeight < 0 时为 top-down，yb = y。
 *
 * Unifont 授权与主页：https://unifoundry.com/unifont.html
 */
#include <drivers/video/cjk_glyphs.h>
#include <linux/types.h>

extern "C" const uint8_t _binary_unifont_bmp_start[];
extern "C" const uint8_t _binary_unifont_bmp_end[];

static uint32_t le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) | (static_cast<uint32_t>(p[3]) << 24u);
}

static uint16_t le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
}

/* 解析 Windows 1bpp BMP 头；成功返回像素区起点与每行字节数（4 字节对齐） */
static bool bmp1bpp_layout(const uint8_t *bmp, uint32_t blob_len, uint32_t *out_w, uint32_t *out_h,
                           uint32_t *out_pix_off, uint32_t *out_row_bytes, int32_t *out_ih_signed)
{
    if (blob_len < 62u || bmp[0] != 'B' || bmp[1] != 'M')
        return false;
    uint32_t pix_off = le32(bmp + 10);
    uint32_t dib = le32(bmp + 14);
    if (14u + dib > blob_len)
        return false;
    int32_t iw = static_cast<int32_t>(le32(bmp + 18));
    int32_t ih = static_cast<int32_t>(le32(bmp + 22));
    uint16_t bpp = le16(bmp + 28);
    if (bpp != 1u || iw <= 0)
        return false;
    uint32_t h = static_cast<uint32_t>(ih < 0 ? -ih : ih);
    uint32_t w = static_cast<uint32_t>(iw);
    uint32_t row_bytes = ((w + 31u) / 32u) * 4u;
    if (pix_off + row_bytes * h > blob_len)
        return false;
    *out_w = w;
    *out_h = h;
    *out_pix_off = pix_off;
    *out_row_bytes = row_bytes;
    if (out_ih_signed)
        *out_ih_signed = ih;
    return true;
}

/* 与 HZK 相同：每行 2 字节，MSB 为左像素；供 blit_mono stride=2 */
static uint8_t s_glyph[32];

static bool bmp_pixel(const uint8_t *bmp, uint32_t h, uint32_t pix_off, uint32_t row_bytes,
                      int32_t ih_signed, int32_t x, int32_t y)
{
    uint32_t yb;
    if (ih_signed > 0)
        yb = h - 1u - static_cast<uint32_t>(y);
    else
        yb = static_cast<uint32_t>(y);
    uint32_t idx = pix_off + yb * row_bytes + static_cast<uint32_t>(x) / 8u;
    int32_t bit = 7 - (x % 8);
    return (bmp[idx] >> bit) & 1;
}

const uint8_t *cjk_glyph_bitmap(uint32_t codepoint)
{
    if (codepoint > 0xffffu)
        return nullptr;

    const uint8_t *base = _binary_unifont_bmp_start;
    uint32_t blob = static_cast<uint32_t>(_binary_unifont_bmp_end - _binary_unifont_bmp_start);
    uint32_t w = 0, h = 0, pix_off = 0, row_bytes = 0;
    int32_t ih_signed = 0;
    if (!bmp1bpp_layout(base, blob, &w, &h, &pix_off, &row_bytes, &ih_signed))
        return nullptr;

    const uint32_t lo = codepoint & 0xffu;
    const uint32_t hi = codepoint >> 8u;
    /* 与 unifontpic 256×256 格一致：第 HH 行即 hi，禁止再用 (hi-8) 误移 CJK（如 U+4E00） */
    const uint32_t row = hi;
    /* 以表位 0041（U+0041）为锚点；坐标为 BMP 视觉像素，见文件头 */
    static constexpr int32_t kGlyphPitch = 16;
    static constexpr uint32_t kAnchorCp = 0x0041u;
    static constexpr int32_t kAnchorCellLeftPx = 1071;
    static constexpr int32_t kAnchorCellTopPx = 63;
    static constexpr int32_t kDataOriginX =
        kAnchorCellLeftPx - static_cast<int32_t>((kAnchorCp & 0xffu) * 16u);
    static constexpr int32_t kDataRowU00TopY =
        kAnchorCellTopPx - static_cast<int32_t>((kAnchorCp >> 8u) * 16u);
    /* 实测字格相对理论 y 略偏下，取样整体下移 1px */
    static constexpr int32_t kBmpGlyphSampleYOffsetPx = 1;
    const int32_t x0 = kDataOriginX + static_cast<int32_t>(lo * static_cast<uint32_t>(kGlyphPitch));
    const int32_t y0 =
        kDataRowU00TopY + static_cast<int32_t>(row * static_cast<uint32_t>(kGlyphPitch)) +
        kBmpGlyphSampleYOffsetPx;
    if (x0 + 16 > static_cast<int32_t>(w) || y0 < 0 || y0 + 16 > static_cast<int32_t>(h))
        return nullptr;

    for (int32_t yy = 0; yy < 16; ++yy) {
        for (int32_t bi = 0; bi < 2; ++bi) {
            uint8_t acc = 0;
            for (int32_t bit = 0; bit < 8; ++bit) {
                int32_t x = x0 + bi * 8 + bit;
                int32_t y = y0 + yy;
                uint8_t px =
                    bmp_pixel(base, h, pix_off, row_bytes, ih_signed, x, y) ? 1u : 0u;
                acc = static_cast<uint8_t>((acc << 1) | px);
            }
            s_glyph[static_cast<uint32_t>(yy) * 2u + static_cast<uint32_t>(bi)] = acc;
        }
    }
    /* 勿再把全 1 格线行清成 0：fb_console blit_mono 已反色（1=纸白、0=墨黑），清 0 会整行变黑 */
    return s_glyph;
}
