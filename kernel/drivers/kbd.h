#ifndef KBD_H
#define KBD_H

#include "kernel_stdint.h"

enum {
    KBD_KEY_UP = 0x100,
    KBD_KEY_DOWN,
    KBD_KEY_LEFT,
    KBD_KEY_RIGHT,
    KBD_KEY_HOME,
    KBD_KEY_END,
    KBD_KEY_DELETE,
    KBD_KEY_F4 = 0x107
};

#define KBD_META 0x200
#define KBD_ALT_F4 (KBD_META | KBD_KEY_F4)
#define KBD_ALT_TAB (KBD_META | '\t')

int kbd_getchar(void);
int kbd_pop(void);
int kbd_pending(void);
void kbd_poll(void);
void kbd_irq_handler(void);

#endif
