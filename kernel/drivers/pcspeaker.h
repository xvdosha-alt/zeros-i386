#ifndef KERNEL_PCSPEAKER_H
#define KERNEL_PCSPEAKER_H

#include "types.h"

/* Play a square-wave tone on the PC speaker for `ms` milliseconds. */
void pcspeaker_beep(uint32_t freq_hz, uint32_t ms);
void pcspeaker_off(void);

#endif
