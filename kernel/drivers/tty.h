#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include "types.h"

void tty_init(void);
void tty_putc(char c);
void tty_write(const char *s);
void tty_writeln(const char *s);
int tty_getc(void);
void tty_set_fg(int pid);
int tty_fg(void);
void tty_clear(void);
void tty_goto(int row, int col);
void tty_get_cursor(int *row, int *col);

#endif
