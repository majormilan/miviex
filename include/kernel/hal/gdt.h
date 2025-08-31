#ifndef GDT_H
#define GDT_H

#include <stdint.h>

int gdt_init();
void tss_set_stack(uint64_t stack);

#endif // GDT_H
