#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include "types.h"

int elf_load(const uint8_t *data, size_t size, uint32_t *entry_out,
             uint8_t **image_out, uint32_t *pages_out, uint32_t *brk_out);

#endif
