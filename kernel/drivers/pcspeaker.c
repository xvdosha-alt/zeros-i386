#include "pcspeaker.h"
#include "io.h"
#include "timer.h"

void pcspeaker_off(void)
{
    outb(0x61, inb(0x61) & 0xFC);
}

void pcspeaker_beep(uint32_t freq_hz, uint32_t ms)
{
    uint32_t div;
    uint32_t start;
    uint32_t wait;

    if (freq_hz < 20u)
        freq_hz = 20u;
    if (freq_hz > 20000u)
        freq_hz = 20000u;
    if (ms == 0)
        return;
    if (ms > 2000u)
        ms = 2000u;

    div = 1193182u / freq_hz;
    outb(0x43, 0xB6); /* ch2, lobyte/hibyte, square wave */
    outb(0x42, (uint8_t)(div & 0xFF));
    outb(0x42, (uint8_t)((div >> 8) & 0xFF));
    outb(0x61, inb(0x61) | 0x03);

    wait = (ms * 100u) / 1000u; /* PIT @ 100 Hz */
    if (wait == 0)
        wait = 1;
    start = timer_ticks();
    /* Syscalls run with IF=0; must sti so PIT advances, else this hangs forever. */
    while ((timer_ticks() - start) < wait)
        __asm__ volatile ("sti; hlt");
    __asm__ volatile ("cli");

    pcspeaker_off();
}
