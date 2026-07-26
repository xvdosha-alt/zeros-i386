#include "ata.h"
#include "io.h"
#include "string.h"
#include "tty.h"

#define ATA_DATA 0x1F0
#define ATA_ERR 0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA0 0x1F3
#define ATA_LBA1 0x1F4
#define ATA_LBA2 0x1F5
#define ATA_DRIVE 0x1F6
#define ATA_CMD 0x1F7
#define ATA_STATUS 0x1F7
#define ATA_ALT 0x3F6

#define ATA_SR_BSY 0x80
#define ATA_SR_DRQ 0x08
#define ATA_SR_ERR 0x01

static uint32_t disk_sectors;
static int ata_present;

static int ata_wait(uint8_t mask, uint8_t val)
{
    int i;
    for (i = 0; i < 100000; i++) {
        uint8_t st = inb(ATA_STATUS);
        if ((st & mask) == val)
            return 0;
        if (st & ATA_SR_ERR)
            return -1;
    }
    return -1;
}

static int ata_wait_bsy(void)
{
    return ata_wait(ATA_SR_BSY, 0);
}

int ata_init(void)
{
    uint16_t id[256];
    int i;
    disk_sectors = 0;
    ata_present = 0;
    outb(ATA_DRIVE, 0xE0);
    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA0, 0);
    outb(ATA_LBA1, 0);
    outb(ATA_LBA2, 0);
    outb(ATA_CMD, 0xEC);
    if (ata_wait_bsy() < 0)
        return -1;
    if (!(inb(ATA_STATUS) & ATA_SR_DRQ)) {
        tty_writeln("[ata] no disk");
        return -1;
    }
    for (i = 0; i < 256; i++)
        id[i] = inw(ATA_DATA);
    disk_sectors = ((uint32_t)id[61] << 16) | id[60];
    if (!disk_sectors)
        disk_sectors = 32768;
    ata_present = 1;
    tty_writeln("[ata] ide0 ready");
    return 0;
}

uint32_t ata_sectors(void)
{
    return disk_sectors;
}

int ata_read(uint32_t lba, void *buf, uint32_t count)
{
    uint16_t *out = (uint16_t *)buf;
    uint32_t s;
    if (!ata_present || !count)
        return -1;
    for (s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        int i;
        if (ata_wait_bsy() < 0)
            return -1;
        outb(ATA_DRIVE, (uint8_t)(0xE0 | ((cur >> 24) & 0x0F)));
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA0, (uint8_t)cur);
        outb(ATA_LBA1, (uint8_t)(cur >> 8));
        outb(ATA_LBA2, (uint8_t)(cur >> 16));
        outb(ATA_CMD, 0x20);
        if (ata_wait_bsy() < 0)
            return -1;
        if (ata_wait(ATA_SR_DRQ, ATA_SR_DRQ) < 0)
            return -1;
        for (i = 0; i < 256; i++)
            out[s * 256 + i] = inw(ATA_DATA);
    }
    return 0;
}

int ata_write(uint32_t lba, const void *buf, uint32_t count)
{
    const uint16_t *in = (const uint16_t *)buf;
    uint32_t s;
    if (!ata_present || !count)
        return -1;
    for (s = 0; s < count; s++) {
        uint32_t cur = lba + s;
        int i;
        uint8_t st;
        if (ata_wait_bsy() < 0)
            return -1;
        outb(ATA_DRIVE, (uint8_t)(0xE0 | ((cur >> 24) & 0x0F)));
        outb(ATA_SECCOUNT, 1);
        outb(ATA_LBA0, (uint8_t)cur);
        outb(ATA_LBA1, (uint8_t)(cur >> 8));
        outb(ATA_LBA2, (uint8_t)(cur >> 16));
        outb(ATA_CMD, 0x30);
        
        for (i = 0; i < 100000; i++) {
            st = inb(ATA_STATUS);
            if (st & ATA_SR_ERR)
                return -1;
            if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ))
                break;
        }
        if (i >= 100000)
            return -1;
        for (i = 0; i < 256; i++)
            outw(ATA_DATA, in[s * 256 + i]);
        
        for (i = 0; i < 100000; i++) {
            st = inb(ATA_STATUS);
            if (st & ATA_SR_ERR)
                return -1;
            if (!(st & ATA_SR_BSY))
                break;
        }
        if (i >= 100000)
            return -1;
        (void)inb(ATA_STATUS);
    }
    return 0;
}
