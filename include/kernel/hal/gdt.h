#ifndef GDT_H
#define GDT_H

#include <stdint.h>

// Selector values, matching the layout built in src/boot/gdt.asm (null,
// kernel code, kernel data, user code, user data, TSS). User selectors OR
// in RPL=3 so they can be loaded/pushed for ring-3 execution.
#define KERNEL_CODE_SEL 0x08
#define KERNEL_DATA_SEL 0x10
#define USER_CODE_SEL   (0x18 | 3)
#define USER_DATA_SEL   (0x20 | 3)
#define TSS_SEL         0x28

int gdt_init();
void tss_set_stack(uint64_t stack);

#endif // GDT_H
