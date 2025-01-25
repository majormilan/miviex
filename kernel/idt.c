#include "idt.h"
#include "memory.h"

/*  Declare an IDT of 256 entries */
struct idt_entry idt[256];
struct idt_ptr idtp;

/*  External function to load the IDT, defined in Assembly */
extern void idt_load();
extern void isr0();

/*  Function to set an IDT entry */
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
  idt[num].base_low = base & 0xFFFF;
  idt[num].base_high = (base >> 16) & 0xFFFF;
  idt[num].selector = sel;
  idt[num].always0 = 0;
  idt[num].flags = flags;
}

/*  Function to initialize the IDT */
void init_idt() {
  idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
  idtp.base = (uint64_t)&idt;

  /*  Clear out the IDT, initialize to zero using the existing memset function */
  memset(&idt, 0, sizeof(struct idt_entry) * 256);

  /*  Set individual entries (example for interrupt 0) */
  idt_set_gate(0, (uint64_t)isr0, 0x08, 0x8E);

  /*  Load the IDT */
  idt_load();
}
