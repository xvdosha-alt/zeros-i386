#include "idt.h"
#include "io.h"
#include "kernel_stdint.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

extern void irq0_stub(void);
extern void irq1_stub(void);
extern void irq12_stub(void);
extern void isr_stub(void);
extern void syscall_stub(void);

static struct idt_entry idt[256];
static struct idt_ptr idtp;

static void idt_set_gate(uint8_t n, uint32_t handler, uint8_t type_attr)
{
    idt[n].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].type_attr = type_attr;
    idt[n].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

static void pic_remap(void)
{
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
}

void idt_init(void)
{
    uint32_t i;
    for (i = 0; i < 256; i++)
        idt_set_gate((uint8_t)i, (uint32_t)isr_stub, 0x8E);
    idt_set_gate(32, (uint32_t)irq0_stub, 0x8E);
    idt_set_gate(33, (uint32_t)irq1_stub, 0x8E);
    idt_set_gate(44, (uint32_t)irq12_stub, 0x8E);
    idt_set_gate(0x80, (uint32_t)syscall_stub, 0xEE);
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint32_t)&idt;
    pic_remap();
    __asm__ volatile ("lidt %0" : : "m"(idtp));
    __asm__ volatile ("sti");
}
