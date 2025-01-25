#ifndef IDT_H
#define IDT_H

/*  Define standard integer types */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

/*  Define an IDT entry */
struct idt_entry {
  uint16_t base_low;
  uint16_t selector;
  uint8_t always0;
  uint8_t flags;
  uint16_t base_high;
} __attribute__((packed));

/*  Define an IDT pointer */
struct idt_ptr {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));

void init_idt();
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);

#endif /*  IDT_H */
