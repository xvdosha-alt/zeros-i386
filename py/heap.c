#include "heap.h"
#include "util.h"

static uint8_t heap_buf[MP_HEAP_SIZE];
static size_t heap_off;

void mp_heap_init(void)
{
    heap_off = 0;
    mp_memset(heap_buf, 0, sizeof(heap_buf));
}

void mp_heap_reset(void)
{
    heap_off = 0;
}

void *mp_alloc(size_t size)
{
    size_t aligned = (size + 7u) & ~7u;
    if (heap_off + aligned > MP_HEAP_SIZE)
        return NULL;
    void *p = &heap_buf[heap_off];
    heap_off += aligned;
    return p;
}

size_t mp_heap_used(void)
{
    return heap_off;
}
