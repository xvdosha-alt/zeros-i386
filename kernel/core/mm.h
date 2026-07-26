#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include "types.h"

#define PAGE_SIZE 4096u
#define KERNEL_HEAP_START 0x00400000u
#define USER_LOAD_BASE 0x02000000u
#define USER_STACK_SIZE 0x20000u

void mm_init(uint32_t multiboot_info);
void *mm_alloc_pages(size_t n);
void mm_free_pages(void *p, size_t n);
void *kmalloc(size_t n);
uint32_t mm_phys_used(void);

#endif
