#include "idt.h"
#include "memory.h"
#include "vga.h"

void trigger_interrupt_0() { asm volatile("int $0"); }

void kernel_main(void) {
  terminal_clear();
  /*  Initialize memory system */
  k_memory_init();
  init_idt();
  terminal_print("MiViE UNIX start\n");
  trigger_interrupt_0();
  while (1) {
    __asm__("hlt");
  }
}
