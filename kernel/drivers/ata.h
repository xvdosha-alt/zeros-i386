#ifndef KERNEL_ATA_H
#define KERNEL_ATA_H

#include "types.h"

int ata_init(void);
int ata_read(uint32_t lba, void *buf, uint32_t count);
int ata_write(uint32_t lba, const void *buf, uint32_t count);
uint32_t ata_sectors(void);

#endif
