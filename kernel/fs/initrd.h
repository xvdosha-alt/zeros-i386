#ifndef KERNEL_INITRD_H
#define KERNEL_INITRD_H

#include "types.h"

void initrd_unpack(const uint8_t *data, uint32_t size);

#endif
