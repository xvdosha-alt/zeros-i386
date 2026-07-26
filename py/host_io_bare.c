#include "host_io.h"
#include "../kernel/drivers/vga.h"
#include "../kernel/drivers/kbd.h"

void mp_io_init(void)
{
}

void mp_putc(char c)
{
    vga_putchar(c);
}

void mp_write(const char *s)
{
    vga_write(s);
}

void mp_writeln(const char *s)
{
    vga_write(s);
    vga_putchar('\n');
}

int mp_getc(void)
{
    return kbd_getchar();
}

int mp_cons_hosted(void)
{
    return 0;
}
