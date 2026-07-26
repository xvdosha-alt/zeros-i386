#include "elf.h"
#include "mm.h"
#include "string.h"
#include "proc.h"

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32Phdr;

#define PT_LOAD 1
#define ET_EXEC 2
#define EM_386 3

int elf_load(const uint8_t *data, size_t size, uint32_t *entry_out,
             uint8_t **image_out, uint32_t *pages_out, uint32_t *brk_out)
{
    const Elf32Ehdr *eh;
    const Elf32Phdr *ph;
    uint32_t i, min_va = 0xFFFFFFFF, max_va = 0;
    uint32_t span, pages;

    if (size < sizeof(Elf32Ehdr))
        return -1;
    eh = (const Elf32Ehdr *)data;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return -1;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_386)
        return -1;
    if (eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(Elf32Phdr) > size)
        return -1;

    ph = (const Elf32Phdr *)(data + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_vaddr < 0x02000000u || ph[i].p_vaddr >= 0x08000000u)
            return -1;
        if (ph[i].p_vaddr < min_va)
            min_va = ph[i].p_vaddr;
        if (ph[i].p_vaddr + ph[i].p_memsz > max_va)
            max_va = ph[i].p_vaddr + ph[i].p_memsz;
    }
    if (min_va == 0xFFFFFFFF || max_va <= min_va)
        return -1;

    /* Swap out any live image that occupies this fixed link address (no MMU). */
    if (proc_evict_range(min_va, max_va) < 0)
        return -2;

    span = max_va - min_va;
    pages = (span + PAGE_SIZE - 1) / PAGE_SIZE;
    kmemset((void *)min_va, 0, pages * PAGE_SIZE);

    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_offset + ph[i].p_filesz > size)
            return -1;
        kmemcpy((void *)ph[i].p_vaddr, data + ph[i].p_offset, ph[i].p_filesz);
        if (ph[i].p_memsz > ph[i].p_filesz)
            kmemset((void *)(ph[i].p_vaddr + ph[i].p_filesz), 0,
                    ph[i].p_memsz - ph[i].p_filesz);
    }

    *entry_out = eh->e_entry;
    *image_out = (uint8_t *)min_va;
    *pages_out = pages;
    *brk_out = max_va;
    return 0;
}
