#include "host_io.h"

void mp_io_init(void)
{
}

void mp_putc(char c)
{
    fputc((unsigned char)c, stdout);
    fflush(stdout);
}

void mp_write(const char *s)
{
    fputs(s, stdout);
    fflush(stdout);
}

void mp_writeln(const char *s)
{
    puts(s);
    fflush(stdout);
}

int mp_getc(void)
{
    int c = fgetc(stdin);
    if (c == EOF)
        return 0;
    return c;
}

int mp_cons_hosted(void)
{
    return 0;
}
