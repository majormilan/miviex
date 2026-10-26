#include <kernel/hal/idt.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/hal/isr.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/syscall/syscall.h>

struct idt_entry idt[256];
struct idt_ptr idtp;

/*  External function to load the IDT, defined in Assembly */
extern void idt_load();

// Addresses of the 256 interrupt stubs (src/boot/isr.asm)
extern const uint64_t isr_table[256];

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_middle = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

/*  Function to initialize the IDT */
int init_idt() {
  idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
  idtp.base = (uint64_t)&idt;

  /*  Clear out the IDT, initialize to zero using the existing memset function */
  memset(&idt, 0, sizeof(struct idt_entry) * 256);

  // Every vector gets its stub (src/boot/isr.asm), kernel-only.
  for (int i = 0; i < 256; i++) {
      idt_set_gate((uint8_t)i, isr_table[i], 0x08, 0x8E);
  }

  /*  Load the IDT */
  idt_load();

  return 0;
}