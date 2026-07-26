#include "mm.h"
#include "string.h"

#define HEAP_END 0x01000000u
#define PAGE_COUNT ((HEAP_END - KERNEL_HEAP_START) / PAGE_SIZE)

static uint8_t page_bitmap[PAGE_COUNT / 8 + 1];
static uint8_t *kheap_ptr;
static uint8_t *kheap_end;

static int bitmap_get(size_t i)
{
    return (page_bitmap[i / 8] >> (i % 8)) & 1;
}

static void bitmap_set(size_t i, int v)
{
    if (v)
        page_bitmap[i / 8] |= (uint8_t)(1u << (i % 8));
    else
        page_bitmap[i / 8] &= (uint8_t)~(1u << (i % 8));
}

void mm_init(uint32_t multiboot_info)
{
    (void)multiboot_info;
    kmemset(page_bitmap, 0, sizeof(page_bitmap));
    kheap_ptr = (uint8_t *)HEAP_END;
    kheap_end = (uint8_t *)0x02000000u;
}

void *mm_alloc_pages(size_t n)
{
    size_t i, j, run;
    if (!n)
        return 0;
    for (i = 0; i < PAGE_COUNT; i++) {
        if (bitmap_get(i))
            continue;
        run = 0;
        for (j = i; j < PAGE_COUNT && run < n; j++) {
            if (bitmap_get(j))
                break;
            run++;
        }
        if (run == n) {
            for (j = 0; j < n; j++)
                bitmap_set(i + j, 1);
            return (void *)(KERNEL_HEAP_START + i * PAGE_SIZE);
        }
    }
    return 0;
}

void mm_free_pages(void *p, size_t n)
{
    uintptr_t a = (uintptr_t)p;
    size_t i, idx;
    if (a < KERNEL_HEAP_START || a >= HEAP_END)
        return;
    idx = (a - KERNEL_HEAP_START) / PAGE_SIZE;
    /* Soft guard: poison freed pages so use-after-free is visible. */
    kmemset(p, 0xA5, n * PAGE_SIZE);
    for (i = 0; i < n && idx + i < PAGE_COUNT; i++)
        bitmap_set(idx + i, 0);
}

void *kmalloc(size_t n)
{
    void *p;
    n = (n + 15u) & ~15u;
    if (kheap_ptr + n > kheap_end)
        return 0;
    p = kheap_ptr;
    kheap_ptr += n;
    return p;
}

uint32_t mm_phys_used(void)
{
    size_t i, c = 0;
    for (i = 0; i < PAGE_COUNT; i++)
        if (bitmap_get(i))
            c++;
    return (uint32_t)(c * PAGE_SIZE);
}
