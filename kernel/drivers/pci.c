#include "pci.h"
#include "io.h"

uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t addr = (uint32_t)(1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t addr = (uint32_t)(1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, value);
}

int pci_find(uint16_t vendor, uint16_t device, uint8_t *bus, uint8_t *slot, uint8_t *func)
{
    uint16_t b, s;
    for (b = 0; b < 256; b++) {
        for (s = 0; s < 32; s++) {
            uint32_t id = pci_read((uint8_t)b, (uint8_t)s, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF)
                continue;
            if ((id & 0xFFFF) == vendor && ((id >> 16) & 0xFFFF) == device) {
                *bus = (uint8_t)b;
                *slot = (uint8_t)s;
                *func = 0;
                return 0;
            }
            if ((id & 0xFFFF) == vendor && device == 0xFFFF) {
                *bus = (uint8_t)b;
                *slot = (uint8_t)s;
                *func = 0;
                return 0;
            }
        }
    }
    return -1;
}
