#include "fb.h"
#include "io.h"
#include "mm.h"
#include "pci.h"
#include "vga.h"

/*
 * Bochs/QEMU VBE (OSDev: Bochs VBE Extensions)
 *   index 0x01CE / data 0x01CF
 *   LFB from PCI BAR0 of 1234:1111 (or 15AD:0405)
 *   Enable with VBE_ENABLED | VBE_LFB
 *
 * Userspace draws into a software backbuffer; fb_flip / fb_flip_rect
 * copy to the hardware LFB so full-screen clears are not visible mid-frame.
 * If the backbuffer cannot be allocated, fall back to direct LFB drawing.
 */

#define VBE_IDX 0x01CE
#define VBE_DAT 0x01CF
#define VBE_ID 0
#define VBE_XRES 1
#define VBE_YRES 2
#define VBE_BPP 3
#define VBE_ENABLE 4
#define VBE_VWIDTH 6
#define VBE_VHEIGHT 7
#define VBE_XOFF 8
#define VBE_YOFF 9
#define VBE_DISABLED 0
#define VBE_ENABLED 1
#define VBE_LFB 0x40

static FbInfo g_fb;
static int g_ready;
static uint32_t g_hw;          /* hardware linear framebuffer */
static uint32_t *g_back;       /* software backbuffer (drawn into) */
static uint32_t g_back_pages;

static void vbe_write(uint16_t index, uint16_t value)
{
    outw(VBE_IDX, index);
    outw(VBE_DAT, value);
}

static uint32_t find_lfb(void)
{
    uint8_t bus, slot, func;
    if (pci_find(0x1234, 0x1111, &bus, &slot, &func) == 0)
        return pci_read(bus, slot, func, 0x10) & ~0xFu;
    if (pci_find(0x15AD, 0x0405, &bus, &slot, &func) == 0)
        return pci_read(bus, slot, func, 0x10) & ~0xFu;
    return 0xE0000000u;
}

static void free_back(void)
{
    if (g_back && g_back_pages) {
        mm_free_pages(g_back, g_back_pages);
        g_back = 0;
        g_back_pages = 0;
    }
}

static uint32_t *draw_ptr(void)
{
    if (!g_ready)
        return 0;
    return g_back ? g_back : (uint32_t *)(uintptr_t)g_hw;
}

int fb_init(uint32_t w, uint32_t h, uint32_t bpp)
{
    uint32_t candidates[4];
    int ci, ok = 0;
    size_t pages;
    uint32_t bytes;

    if (w == 0)
        w = 1024;
    if (h == 0)
        h = 768;
    if (bpp != 32)
        bpp = 32;

    free_back();
    g_ready = 0;
    g_hw = 0;

    vga_font_save();

    candidates[0] = find_lfb();
    candidates[1] = 0xE0000000u;
    candidates[2] = 0xFD000000u;
    candidates[3] = 0xF0000000u;

    vbe_write(VBE_ID, 0xB0C5);
    vbe_write(VBE_ENABLE, VBE_DISABLED);
    vbe_write(VBE_XRES, (uint16_t)w);
    vbe_write(VBE_YRES, (uint16_t)h);
    vbe_write(VBE_BPP, (uint16_t)bpp);
    vbe_write(VBE_VWIDTH, (uint16_t)w);
    vbe_write(VBE_VHEIGHT, (uint16_t)h);
    vbe_write(VBE_XOFF, 0);
    vbe_write(VBE_YOFF, 0);
    vbe_write(VBE_ENABLE, (uint16_t)(VBE_ENABLED | VBE_LFB));

    for (ci = 0; ci < 4; ci++) {
        uint32_t lfb = candidates[ci];
        volatile uint32_t *p;
        int dup, d;
        if (!lfb || lfb < 0x80000000u)
            continue;
        dup = 0;
        for (d = 0; d < ci; d++)
            if (candidates[d] == lfb)
                dup = 1;
        if (dup)
            continue;
        p = (volatile uint32_t *)(uintptr_t)lfb;
        p[0] = 0x11223344u;
        if (p[0] != 0x11223344u)
            continue;
        g_hw = lfb;
        ok = 1;
        break;
    }
    if (!ok) {
        vbe_write(VBE_ENABLE, VBE_DISABLED);
        g_hw = 0;
        g_ready = 0;
        vga_set_mode3();
        vga_font_restore();
        vga_init();
        return -1;
    }

    bytes = w * h * (bpp / 8);
    pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    g_back = (uint32_t *)mm_alloc_pages(pages);
    if (g_back)
        g_back_pages = (uint32_t)pages;
    else {
        g_back_pages = 0;
        /* Direct LFB fallback — still usable, just may flicker. */
    }

    g_fb.width = w;
    g_fb.height = h;
    g_fb.pitch = w * (bpp / 8);
    g_fb.bpp = bpp;
    g_fb.addr = g_back ? (uint32_t)(uintptr_t)g_back : g_hw;
    g_ready = 1;
    fb_fill(0x00101820u);
    fb_flip();
    return 0;
}

void fb_textmode(void)
{
    free_back();
    vbe_write(VBE_ID, 0xB0C5);
    vbe_write(VBE_ENABLE, VBE_DISABLED);
    g_ready = 0;
    g_hw = 0;
    vga_set_mode3();
    vga_font_restore();
    vga_init();
}

int fb_ready(void)
{
    return g_ready;
}

void fb_info(FbInfo *out)
{
    if (out)
        *out = g_fb;
}

uint32_t *fb_ptr(void)
{
    return draw_ptr();
}

uint32_t fb_lfb(void)
{
    return g_ready ? g_hw : 0;
}

void fb_flip(void)
{
    uint32_t i, n;
    uint32_t *src;
    uint32_t *dst;
    if (!g_ready || !g_hw || !g_back)
        return;
    src = g_back;
    dst = (uint32_t *)(uintptr_t)g_hw;
    n = g_fb.width * g_fb.height;
    for (i = 0; i < n; i++)
        dst[i] = src[i];
}

void fb_flip_rect(int x, int y, int w, int h)
{
    int x0, y0, x1, y1, yy, xx;
    uint32_t *src;
    uint32_t *dst;
    if (!g_ready || !g_hw || !g_back || w <= 0 || h <= 0)
        return;
    src = g_back;
    dst = (uint32_t *)(uintptr_t)g_hw;
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > (int)g_fb.width)
        x1 = (int)g_fb.width;
    if (y1 > (int)g_fb.height)
        y1 = (int)g_fb.height;
    for (yy = y0; yy < y1; yy++) {
        uint32_t row = (uint32_t)yy * g_fb.width;
        for (xx = x0; xx < x1; xx++)
            dst[row + (uint32_t)xx] = src[row + (uint32_t)xx];
    }
}

void fb_fill(uint32_t color)
{
    uint32_t i, n;
    uint32_t *dst = draw_ptr();
    if (!dst)
        return;
    n = g_fb.width * g_fb.height;
    for (i = 0; i < n; i++)
        dst[i] = color;
}

void fb_pixel(int x, int y, uint32_t color)
{
    uint32_t *dst = draw_ptr();
    if (!dst || x < 0 || y < 0 ||
        (uint32_t)x >= g_fb.width || (uint32_t)y >= g_fb.height)
        return;
    dst[y * g_fb.width + x] = color;
}

void fb_rect(int x, int y, int w, int h, uint32_t color)
{
    int x0, y0, x1, y1, xx, yy;
    uint32_t *dst = draw_ptr();
    if (!dst || w <= 0 || h <= 0)
        return;
    x0 = x < 0 ? 0 : x;
    y0 = y < 0 ? 0 : y;
    x1 = x + w;
    y1 = y + h;
    if (x1 > (int)g_fb.width)
        x1 = (int)g_fb.width;
    if (y1 > (int)g_fb.height)
        y1 = (int)g_fb.height;
    for (yy = y0; yy < y1; yy++) {
        uint32_t *row = dst + yy * g_fb.width;
        for (xx = x0; xx < x1; xx++)
            row[xx] = color;
    }
}

void fb_rect_border(int x, int y, int w, int h, uint32_t fill, uint32_t border)
{
    fb_rect(x, y, w, h, fill);
    fb_rect(x, y, w, 1, border);
    fb_rect(x, y + h - 1, w, 1, border);
    fb_rect(x, y, 1, h, border);
    fb_rect(x + w - 1, y, 1, h, border);
}

void fb_blit(int x, int y, const uint32_t *src, int w, int h, int src_pitch)
{
    int yy, xx;
    uint32_t *dst = draw_ptr();
    if (!dst || !src || w <= 0 || h <= 0)
        return;
    for (yy = 0; yy < h; yy++) {
        int dy = y + yy;
        if (dy < 0 || (uint32_t)dy >= g_fb.height)
            continue;
        for (xx = 0; xx < w; xx++) {
            int dx = x + xx;
            if (dx < 0 || (uint32_t)dx >= g_fb.width)
                continue;
            dst[dy * g_fb.width + dx] = src[yy * src_pitch + xx];
        }
    }
}
