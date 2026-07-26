#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include "types.h"

void timer_init(uint32_t hz);
uint32_t timer_ticks(void);
void timer_irq(void);

#endif
