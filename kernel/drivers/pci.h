#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include "types.h"

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
int pci_find(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *slot, uint8_t *func);

#endif
