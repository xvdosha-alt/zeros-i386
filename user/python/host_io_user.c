#include "../../py/host_io.h"
#include "libmp.h"

void mp_io_init(void)
{
}

void mp_putc(char c)
{
    sys_write(1, &c, 1);
}

void mp_write(const char *s)
{
    sys_write(1, s, strlen_u(s));
}

void mp_writeln(const char *s)
{
    mp_write(s);
    mp_putc('\n');
}

int mp_getc(void)
{
    int key = 0;
    if (sys_read(0, &key, 4) != 4)
        return -1;
    if (key == '\r')
        return '\n';
    return key;
}

int mp_cons_hosted(void)
{
    return sys_cons_hosted();
}
