#ifndef MP_HEAP_H
#define MP_HEAP_H

#include "config.h"

void mp_heap_init(void);
void *mp_alloc(size_t size);
void mp_heap_reset(void);
size_t mp_heap_used(void);

#endif
