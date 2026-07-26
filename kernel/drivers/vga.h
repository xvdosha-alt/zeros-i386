#ifndef VGA_H
#define VGA_H

#include "kernel_stdint.h"
#include "kernel_stddef.h"

void vga_init(void);
void vga_putchar(char c);
void vga_write(const char *s);
void vga_move_up(void);
void vga_move_down(void);
void vga_move_left(void);
void vga_move_right(void);
void vga_set_pos(size_t r, size_t c);
void vga_get_pos(size_t *r, size_t *c);

/* Save BIOS 8x16 font before VBE; reload when returning to text mode. */
void vga_font_save(void);
void vga_font_restore(void);
void vga_set_mode3(void);

#endif
