#include "tty.h"
#include "vga.h"
#include "kbd.h"
#include "io.h"

static int fg_pid = 1;

void tty_init(void)
{
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x80);
    outb(0x3F8 + 0, 0x03);
    outb(0x3F8 + 1, 0x00);
    outb(0x3F8 + 3, 0x03);
    outb(0x3F8 + 2, 0xC7);
    outb(0x3F8 + 4, 0x0B);
    vga_init();
    fg_pid = 1;
}

void tty_putc(char c)
{
    outb(0xE9, (uint8_t)c);
    outb(0x3F8, (uint8_t)c);
    if (c == '\n')
        outb(0x3F8, '\r');
    vga_putchar(c);
}

void tty_write(const char *s)
{
    while (*s)
        tty_putc(*s++);
}

void tty_writeln(const char *s)
{
    tty_write(s);
    tty_putc('\n');
}

static int serial_pending(void)
{
    return (inb(0x3F8 + 5) & 0x01) != 0;
}

static int serial_getc(void)
{
    unsigned char c = inb(0x3F8);
    if (c == 0x7F)
        return '\b';
    if (c == '\r')
        return '\n';
    return (int)c;
}

int tty_getc(void)
{
    for (;;) {
        __asm__ volatile ("sti");
        if (serial_pending())
            return serial_getc();
        kbd_poll();
        if (kbd_pending())
            return kbd_getchar();
        __asm__ volatile ("hlt");
    }
}

void tty_set_fg(int pid)
{
    fg_pid = pid;
}

int tty_fg(void)
{
    return fg_pid;
}

void tty_clear(void)
{
    vga_init();
}

void tty_goto(int row, int col)
{
    extern void vga_set_pos(size_t r, size_t c);
    vga_set_pos((size_t)row, (size_t)col);
}

void tty_get_cursor(int *row, int *col)
{
    extern void vga_get_pos(size_t *r, size_t *c);
    size_t r, c;
    vga_get_pos(&r, &c);
    if (row)
        *row = (int)r;
    if (col)
        *col = (int)c;
}
