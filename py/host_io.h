#ifndef MP_HOST_IO_H
#define MP_HOST_IO_H

#include "config.h"

void mp_io_init(void);
void mp_putc(char c);
void mp_write(const char *s);
void mp_writeln(const char *s);
int mp_getc(void);
int mp_cons_hosted(void);

#endif
