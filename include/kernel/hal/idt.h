#ifndef IDT_H
#define IDT_H

#include <kernel/types.h>

/*  Define an IDT entry for x86_64 */
struct idt_entry {
  uint16_t base_low;      // Low 16 bits of the address
  uint16_t selector;      // Kernel segment selector
  uint8_t always0;        // Always set to 0
  uint8_t flags;          // Flags (type and attributes)
  uint16_t base_middle;   // Next 16 bits of the address
  uint32_t base_high;     // High 32 bits of the address
  uint32_t reserved;      // Reserved, set to 0
} __attribute__((packed));

/*  Define an IDT pointer */
struct idt_ptr {
  uint16_t limit;         // Limit of the IDT (size - 1)
  uint64_t base;          // Base address of the IDT
} __attribute__((packed));

/* Function declarations */
void init_idt();
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);

/* External declarations for ISR handlers */
extern void isr0_handler();
extern void isr32_handler();
extern void isr33_handler();

#endif /*  IDT_H */
