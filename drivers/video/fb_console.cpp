/*
 * drivers/video/fb_console.cpp — 帧缓冲 UTF-8 控制台：
 * Multiboot 1 帧信息 + Unifont BMP（16×16；行/像素取法见 drivers/video/cjk_glyphs.cpp）。
 */
#include <drivers/video/cjk_glyphs.h>
#include <drivers/video/fb_console.h>
#include <linux/types.h>

static constexpr uint32_t kMultibootMagic = 0x2BADB002u;
static constexpr uint32_t kMbFlagFramebuffer = 0x1000u;

static uint8_t *g_fb = nullptr;
static uint32_t g_w = 0, g_h = 0, g_pitch = 0;
static bool g_active = false;

static int32_t g_pen_x = 0, g_pen_y = 0;
static int32_t g_margin = 8;
static int32_t g_last_adv = 8;
/* 当前行 mono 已画到的最右像素 x（含）；紧排英文后笔位偏左，整格符号/CJK 会向左盖住英文 */
static int32_t g_mono_right_x = -1;
/* 行高略大于 16px 字模，换行时留竖向间隙；滚动步进与之一致 */
static constexpr int32_t kCellH = 18;
static uint32_t g_fg = 0;
static uint32_t g_bg = 0;

static uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xff000000u | static_cast<uint32_t>(b) | (static_cast<uint32_t>(g) << 8u) |
           (static_cast<uint32_t>(r) << 16u);
}

static void put_px(int32_t x, int32_t y, uint32_t c)
{
    if (x < 0 || y < 0 || static_cast<uint32_t>(x) >= g_w || static_cast<uint32_t>(y) >= g_h)
        return;
    uint32_t *p = reinterpret_cast<uint32_t *>(g_fb + static_cast<uint32_t>(y) * g_pitch +
                                                 static_cast<uint32_t>(x) * 4u);
    *p = c;
}

static void fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t c)
{
    for (int32_t yy = 0; yy < h; ++yy)
        for (int32_t xx = 0; xx < w; ++xx)
            put_px(x + xx, y + yy, c);
}

static void scroll_up(int32_t dy)
{
    if (dy <= 0 || static_cast<uint32_t>(dy) >= g_h)
        return;
    uint32_t move = g_h - static_cast<uint32_t>(dy);
    for (uint32_t row = 0; row < move; ++row) {
        uint32_t *dst = reinterpret_cast<uint32_t *>(g_fb + row * g_pitch);
        const uint32_t *src = reinterpret_cast<const uint32_t *>(g_fb + (row + static_cast<uint32_t>(dy)) * g_pitch);
        for (uint32_t x = 0; x < g_w; ++x)
            dst[x] = src[x];
    }
    for (uint32_t row = move; row < g_h; ++row) {
        uint32_t *ln = reinterpret_cast<uint32_t *>(g_fb + row * g_pitch);
        for (uint32_t x = 0; x < g_w; ++x)
            ln[x] = g_bg;
    }
}

static int32_t content_right()
{
    return static_cast<int32_t>(g_w) - g_margin;
}

static int32_t content_bottom()
{
    return static_cast<int32_t>(g_h) - g_margin;
}

static void ensure_room_for_bottom(int32_t ext)
{
    while (g_pen_y + ext > content_bottom())
        scroll_up(kCellH), g_pen_y -= kCellH;
    if (g_pen_y < g_margin)
        g_pen_y = g_margin;
}

static void newline()
{
    g_pen_x = g_margin;
    g_pen_y += kCellH;
    g_mono_right_x = g_margin - 1;
    ensure_room_for_bottom(kCellH);
}

static void tabto()
{
    const int32_t tabw = 8 * 16;
    int32_t rel = g_pen_x - g_margin;
    if (rel < 0)
        rel = 0;
    int32_t nx = g_margin + ((rel + tabw) / tabw) * tabw;
    g_pen_x = nx;
    if (g_pen_x + 16 > content_right())
        newline();
}

static void backsp()
{
    if (g_pen_x > g_margin) {
        g_pen_x -= g_last_adv;
        fill_rect(g_pen_x, g_pen_y, g_last_adv, kCellH, g_bg);
        g_mono_right_x = g_pen_x - 1;
    }
}

static bool read_multiboot_fb(const void *mbi, uint8_t **out_bytes, uint32_t *out_w, uint32_t *out_h,
                              uint32_t *out_pitch, uint8_t *out_bpp, uint8_t *out_type)
{
    const uint8_t *p = static_cast<const uint8_t *>(mbi);
    uint32_t flags = *reinterpret_cast<const uint32_t *>(p);
    if (!(flags & kMbFlagFramebuffer))
        return false;
    uint64_t addr = *reinterpret_cast<const uint64_t *>(p + 88);
    uint32_t pitch = *reinterpret_cast<const uint32_t *>(p + 96);
    uint32_t w = *reinterpret_cast<const uint32_t *>(p + 100);
    uint32_t h = *reinterpret_cast<const uint32_t *>(p + 104);
    uint8_t bpp = *reinterpret_cast<const uint8_t *>(p + 108);
    uint8_t type = *reinterpret_cast<const uint8_t *>(p + 109);
    if (!w || !h || !pitch)
        return false;
    *out_bytes = reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(addr & 0xffffffffu));
    *out_w = w;
    *out_h = h;
    *out_pitch = pitch;
    *out_bpp = bpp;
    *out_type = type;
    return true;
}

static void blit_mono(int32_t x0, int32_t y0, const uint8_t *bits, int32_t gw, int32_t gh, int32_t stride)
{
    for (int32_t row = 0; row < gh; ++row) {
        for (int32_t col = 0; col < gw; ++col) {
            uint8_t b = bits[static_cast<uint32_t>(row) * static_cast<uint32_t>(stride) + col / 8];
            int32_t bit = 7 - (col % 8);
            bool on = (b >> bit) & 1;
            /* Unifont 1bpp BMP 位为 1 处多为纸白；反相后笔画用 g_fg（黑）、底用 g_bg（白） */
            put_px(x0 + col, y0 + row, on ? g_bg : g_fg);
        }
    }
}

/* 与 blit_mono 一致：位为 0 表示反色后的黑墨 */
static bool mono_ink_at(const uint8_t *bits, int32_t stride, int32_t gw, int32_t gh, int32_t col, int32_t row)
{
    if (col < 0 || col >= gw || row < 0 || row >= gh)
        return false;
    uint8_t b = bits[static_cast<uint32_t>(row) * static_cast<uint32_t>(stride) + col / 8];
    int32_t bit = 7 - (col % 8);
    return ((b >> bit) & 1) == 0;
}

/* 基本拉丁 0x20–0x7E：按墨迹左右边界紧排 */
static constexpr int32_t kLatinTightRightPad = 1;
/* 略小于整格 (gw-lcol)，略增 slack 则字距更近；叠字时再减小 slack 或增大 pad */
static constexpr int32_t kLatinTileClearSlack = 5;
/* 空格 U+0020：全图格内常有格线，勿按墨迹裁切；步进约为半宽 */
static constexpr int32_t kLatinSpaceAdvancePx = 10;

static bool latin_use_tight_spacing(uint32_t cp)
{
    return cp >= 0x20u && cp <= 0x7eu;
}

/* 返回步进宽度；*lcol_out / *rcol_out 为字模内墨迹最左、最右列（rcol=-1 表示空格，由调用方单独处理） */
static int32_t latin_tight_advance(uint32_t cp, const uint8_t *bits, int32_t stride, int32_t gw, int32_t gh,
                                   int32_t *lcol_out, int32_t *rcol_out)
{
    *lcol_out = 0;
    *rcol_out = -1;
    if (cp == 0x20u) {
        /* 必须在墨迹扫描之前返回：否则格线会被当成墨，lcol/adv 错乱 */
        return kLatinSpaceAdvancePx;
    }
    int32_t lcol = gw;
    int32_t rcol = -1;
    for (int32_t col = 0; col < gw; ++col) {
        for (int32_t row = 0; row < gh; ++row) {
            if (mono_ink_at(bits, stride, gw, gh, col, row)) {
                if (col < lcol)
                    lcol = col;
                if (col > rcol)
                    rcol = col;
            }
        }
    }
    if (rcol < lcol) {
        *rcol_out = gw - 1;
        return gw;
    }
    *lcol_out = lcol;
    *rcol_out = rcol;
    const int32_t ink_w = rcol - lcol + 1;
    const int32_t from_ink = ink_w + kLatinTightRightPad;
    const int32_t from_cell = gw - lcol - kLatinTileClearSlack;
    /* max(墨迹+边, 近整格)：比纯 gw-lcol 略窄，比仅 ink+1 不易叠 */
    int32_t adv = from_ink > from_cell ? from_ink : from_cell;
    return adv;
}

static bool glyph_lookup(uint32_t cp, const uint8_t **bits, int32_t *gw, int32_t *gh, int32_t *stride)
{
    const uint8_t *cj = cjk_glyph_bitmap(cp);
    if (!cj)
        return false;
    *bits = cj;
    *gw = 16;
    *gh = 16;
    *stride = 2;
    return true;
}

static void draw_codepoint(uint32_t cp)
{
    const uint8_t *bits = nullptr;
    int32_t gw = 0, gh = 0, st = 0;
    uint32_t lookup_cp = cp;
    if (!glyph_lookup(cp, &bits, &gw, &gh, &st)) {
        lookup_cp = static_cast<uint32_t>('?');
        glyph_lookup(lookup_cp, &bits, &gw, &gh, &st);
    }

    int32_t lcol = 0;
    int32_t ink_rcol = gw - 1;
    int32_t adv = gw;
    if (latin_use_tight_spacing(lookup_cp))
        adv = latin_tight_advance(lookup_cp, bits, st, gw, gh, &lcol, &ink_rcol);

    if (g_pen_x + adv > content_right())
        newline();
    /* 按本字实际高度留底边，避免笔画超出屏幕下缘被 put_px 裁掉 */
    ensure_room_for_bottom(gh);

    int32_t y0 = g_pen_y;
    if (gh < kCellH)
        y0 += (kCellH - gh) / 2;

    int32_t draw_x = g_pen_x - lcol;
    if (draw_x <= g_mono_right_x)
        g_pen_x = g_mono_right_x + 1 + lcol;
    draw_x = g_pen_x - lcol;
    /* 避开英文后笔位仍偏左，整格符号会向左盖住墨迹 */
    if (draw_x + gw > content_right()) {
        newline();
        draw_x = g_pen_x - lcol;
        if (draw_x <= g_mono_right_x)
            g_pen_x = g_mono_right_x + 1 + lcol;
        draw_x = g_pen_x - lcol;
    }

    if (lookup_cp == 0x20u) {
        fill_rect(g_pen_x, g_pen_y, adv, kCellH, g_bg);
        g_mono_right_x = g_pen_x + adv - 1;
    } else {
        blit_mono(draw_x, y0, bits, gw, gh, st);
        /* 紧排英文只推进到墨迹右缘，勿用整格 16 列，否则下一英文字会被误右移、字距变大 */
        if (latin_use_tight_spacing(lookup_cp) && ink_rcol >= 0)
            g_mono_right_x = draw_x + ink_rcol;
        else
            g_mono_right_x = draw_x + gw - 1;
    }
    g_pen_x += adv;
    g_last_adv = adv;
}

static bool utf8_pull(const uint8_t *&s, uint32_t &cp)
{
    uint8_t c0 = *s;
    if (c0 == 0)
        return false;
    if (c0 < 0x80u) {
        cp = c0;
        ++s;
        return true;
    }
    if ((c0 & 0xe0u) == 0xc0u) {
        if (s[1] == 0) {
            cp = '?';
            ++s;
            return true;
        }
        uint8_t c1 = s[1];
        if ((c1 & 0xc0u) != 0x80u || c0 < 0xc2u) {
            cp = '?';
            ++s;
            return true;
        }
        cp = (static_cast<uint32_t>(c0 & 0x1fu) << 6) | (c1 & 0x3fu);
        s += 2;
        return true;
    }
    if ((c0 & 0xf0u) == 0xe0u) {
        if (s[1] == 0 || s[2] == 0) {
            cp = '?';
            ++s;
            return true;
        }
        uint8_t c1 = s[1], c2 = s[2];
        if ((c1 & 0xc0u) != 0x80u || (c2 & 0xc0u) != 0x80u) {
            cp = '?';
            ++s;
            return true;
        }
        cp = (static_cast<uint32_t>(c0 & 0x0fu) << 12) | (static_cast<uint32_t>(c1 & 0x3fu) << 6) |
             (c2 & 0x3fu);
        if (cp < 0x800u || (cp >= 0xd800u && cp <= 0xdfffu)) {
            cp = '?';
            s += 3;
            return true;
        }
        s += 3;
        return true;
    }
    if ((c0 & 0xf8u) == 0xf0u) {
        if (s[1] == 0 || s[2] == 0 || s[3] == 0) {
            cp = '?';
            ++s;
            return true;
        }
        uint8_t c1 = s[1], c2 = s[2], c3 = s[3];
        if ((c1 & 0xc0u) != 0x80u || (c2 & 0xc0u) != 0x80u || (c3 & 0xc0u) != 0x80u) {
            cp = '?';
            ++s;
            return true;
        }
        cp = (static_cast<uint32_t>(c0 & 0x07u) << 18) | (static_cast<uint32_t>(c1 & 0x3fu) << 12) |
             (static_cast<uint32_t>(c2 & 0x3fu) << 6) | (c3 & 0x3fu);
        if (cp < 0x10000u || cp > 0x10ffffu) {
            cp = '?';
            s += 4;
            return true;
        }
        s += 4;
        return true;
    }
    cp = 0xfffdu;
    ++s;
    return true;
}

extern "C" void fb_console_init(const void *multiboot_info, uint32_t multiboot_magic)
{
    g_active = false;
    if (multiboot_magic != kMultibootMagic)
        return;

    uint8_t *bytes = nullptr;
    uint32_t w = 0, h = 0, pitch = 0;
    uint8_t bpp = 0, typ = 0;
    if (!read_multiboot_fb(multiboot_info, &bytes, &w, &h, &pitch, &bpp, &typ))
        return;
    if (bpp != 32u || typ != 1u)
        return;

    g_fb = bytes;
    g_w = w;
    g_h = h;
    g_pitch = pitch;
    g_fg = pack_rgb(0, 0, 0);
    g_bg = pack_rgb(255, 255, 255);
    g_margin = 8;
    g_pen_x = g_margin;
    g_pen_y = g_margin;
    g_last_adv = 16;
    g_mono_right_x = g_margin - 1;

    for (uint32_t y = 0; y < g_h; ++y) {
        uint32_t *ln = reinterpret_cast<uint32_t *>(g_fb + y * g_pitch);
        for (uint32_t x = 0; x < g_w; ++x)
            ln[x] = g_bg;
    }
    g_active = true;
}

extern "C" bool fb_console_active(void)
{
    return g_active;
}

extern "C" void fb_console_puts(const int8_t *utf8)
{
    if (!g_active || !utf8)
        return;

    const uint8_t *p = reinterpret_cast<const uint8_t *>(utf8);
    while (*p) {
        uint8_t c = *p;
        if (c == '\n') {
            ++p;
            newline();
            continue;
        }
        if (c == '\b') {
            ++p;
            backsp();
            continue;
        }
        if (c == '\r') {
            ++p;
            g_pen_x = g_margin;
            g_mono_right_x = g_margin - 1;
            continue;
        }
        if (c == '\t') {
            ++p;
            tabto();
            continue;
        }
        if (c < 0x20u) {
            ++p;
            continue;
        }

        uint32_t cp;
        if (!utf8_pull(p, cp))
            break;
        draw_codepoint(cp);
    }
}
