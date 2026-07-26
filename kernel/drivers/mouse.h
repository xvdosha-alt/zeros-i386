#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include "types.h"

typedef struct {
    int x;
    int y;
    int buttons;
    int dx;
    int dy;
} MouseState;

void mouse_init(void);
void mouse_poll(void);
void mouse_irq_handler(void);
void mouse_byte(uint8_t data);
void mouse_state(MouseState *out);
void mouse_coords(int *x, int *y, int *buttons);
int mouse_moved(void);
void mouse_set_bounds(int w, int h);
void mouse_enable_stream(void);

#endif
