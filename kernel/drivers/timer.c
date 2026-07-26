#include "timer.h"
#include "io.h"
#include "net.h"

static volatile uint32_t ticks;

void timer_init(uint32_t hz)
{
    uint32_t div = 1193182u / (hz ? hz : 100u);
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(div & 0xFF));
    outb(0x40, (uint8_t)((div >> 8) & 0xFF));
    ticks = 0;
}

uint32_t timer_ticks(void)
{
    return ticks;
}

void timer_irq(void)
{
    ticks++;
    net_poll();
}
