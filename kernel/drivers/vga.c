#include "vga.h"
#include "io.h"
#include "types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
#define VGA_COLOR  0x07
#define VGA_FONT_H 16

static size_t row;
static size_t col;
static uint8_t g_font[256 * VGA_FONT_H];
static int g_font_ok;

static void update_cursor(void)
{
    uint16_t pos = (uint16_t)(row * VGA_WIDTH + col);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void)
{
    size_t i;

    for (i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        VGA_MEMORY[i] = VGA_MEMORY[i + VGA_WIDTH];

    for (i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
        VGA_MEMORY[i] = (uint16_t)VGA_COLOR << 8 | ' ';

    row = VGA_HEIGHT - 1;
}

static void newline(void)
{
    col = 0;
    row++;
    if (row >= VGA_HEIGHT)
        scroll();
}

static void font_plane_enter(void)
{
    /* Map plane 2 at 0xA0000 for sequential access (OSDev VGA Fonts). */
    outw(0x3CE, 0x0005);
    outw(0x3CE, 0x0406);
    outw(0x3C4, 0x0402);
    outw(0x3C4, 0x0604);
}

static void font_plane_leave(void)
{
    outw(0x3C4, 0x0302);
    outw(0x3C4, 0x0204);
    outw(0x3CE, 0x1005);
    outw(0x3CE, 0x0E06);
}

void vga_font_save(void)
{
    volatile uint8_t *vga = (volatile uint8_t *)(uintptr_t)0xA0000;
    int i, r;

    /* Only capture while the BIOS font is still intact (first graphics enter). */
    if (g_font_ok)
        return;

    font_plane_enter();
    for (i = 0; i < 256; i++) {
        for (r = 0; r < VGA_FONT_H; r++)
            g_font[i * VGA_FONT_H + r] = vga[i * 32 + r];
    }
    font_plane_leave();
    g_font_ok = 1;
}

void vga_font_restore(void)
{
    volatile uint8_t *vga = (volatile uint8_t *)(uintptr_t)0xA0000;
    int i, r;

    if (!g_font_ok)
        return;
    font_plane_enter();
    for (i = 0; i < 256; i++) {
        for (r = 0; r < VGA_FONT_H; r++)
            vga[i * 32 + r] = g_font[i * VGA_FONT_H + r];
        for (; r < 32; r++)
            vga[i * 32 + r] = 0;
    }
    font_plane_leave();
}

void vga_set_mode3(void)
{
    static const uint8_t seq[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
    static const uint8_t crtc[25] = {
        0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
        0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
        0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
        0xFF
    };
    static const uint8_t gc[9] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF
    };
    static const uint8_t attr[21] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
        0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
        0x0C, 0x00, 0x0F, 0x08, 0x00
    };
    /* Standard 16-color VGA DAC (6-bit components). */
    static const uint8_t dac[16][3] = {
        {0,0,0}, {0,0,42}, {0,42,0}, {0,42,42},
        {42,0,0}, {42,0,42}, {42,21,0}, {42,42,42},
        {21,21,21}, {21,21,63}, {21,63,21}, {21,63,63},
        {63,21,21}, {63,21,63}, {63,63,21}, {63,63,63}
    };
    int i;

    /* Blank attribute controller while reprogramming. */
    inb(0x3DA);
    outb(0x3C0, 0x00);

    outb(0x3C2, 0x67);

    outb(0x3C4, 0x00);
    outb(0x3C5, 0x01);
    for (i = 1; i < 5; i++) {
        outb(0x3C4, (uint8_t)i);
        outb(0x3C5, seq[i]);
    }
    outb(0x3C4, 0x00);
    outb(0x3C5, 0x03);

    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);

    for (i = 0; i < 25; i++) {
        outb(0x3D4, (uint8_t)i);
        outb(0x3D5, crtc[i]);
    }

    for (i = 0; i < 9; i++) {
        outb(0x3CE, (uint8_t)i);
        outb(0x3CF, gc[i]);
    }

    inb(0x3DA);
    for (i = 0; i < 21; i++) {
        outb(0x3C0, (uint8_t)i);
        outb(0x3C0, attr[i]);
    }

    outb(0x3C6, 0xFF);
    outb(0x3C8, 0);
    for (i = 0; i < 16; i++) {
        outb(0x3C9, dac[i][0]);
        outb(0x3C9, dac[i][1]);
        outb(0x3C9, dac[i][2]);
    }

    inb(0x3DA);
    outb(0x3C0, 0x20);
}

void vga_init(void)
{
    volatile uint16_t *p = (volatile uint16_t *)0xB8000;
    int i;

    row = 0;
    col = 0;
    for (i = 0; i < 80 * 25; i++)
        p[i] = (uint16_t)((VGA_COLOR << 8) | ' ');
    update_cursor();
}

void vga_putchar(char c)
{
    if (c == '\n') {
        newline();
        update_cursor();
        return;
    }

    if (c == '\r') {
        col = 0;
        update_cursor();
        return;
    }

    if (c == '\b') {
        if (col > 0) {
            col--;
        } else if (row > 0) {
            row--;
            col = VGA_WIDTH - 1;
        }
        VGA_MEMORY[row * VGA_WIDTH + col] = (uint16_t)VGA_COLOR << 8 | ' ';
        update_cursor();
        return;
    }

    if (c < 32)
        return;

    VGA_MEMORY[row * VGA_WIDTH + col] = (uint16_t)VGA_COLOR << 8 | (uint8_t)c;
    col++;
    if (col >= VGA_WIDTH)
        newline();
    update_cursor();
}

void vga_write(const char *s)
{
    while (*s)
        vga_putchar(*s++);
}

void vga_move_up(void)
{
    if (row > 0)
        row--;
    update_cursor();
}

void vga_move_down(void)
{
    if (row + 1 < VGA_HEIGHT)
        row++;
    update_cursor();
}

void vga_move_left(void)
{
    if (col > 0) {
        col--;
    } else if (row > 0) {
        row--;
        col = VGA_WIDTH - 1;
    }
    update_cursor();
}

void vga_move_right(void)
{
    if (col + 1 < VGA_WIDTH) {
        col++;
    } else if (row + 1 < VGA_HEIGHT) {
        row++;
        col = 0;
    }
    update_cursor();
}

void vga_set_pos(size_t r, size_t c)
{
    if (r >= VGA_HEIGHT)
        r = VGA_HEIGHT - 1;
    if (c >= VGA_WIDTH)
        c = VGA_WIDTH - 1;
    row = r;
    col = c;
    update_cursor();
}

void vga_get_pos(size_t *r, size_t *c)
{
    if (r)
        *r = row;
    if (c)
        *c = col;
}
