#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

void pmm_init();
void* pmm_alloc_page();
void pmm_free_page(void* page);

#endif // PMM_H
