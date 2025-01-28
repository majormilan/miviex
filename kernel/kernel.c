#include "idt.h"
#include "memory.h"
#include "vga.h"
#include "pic.h"

void trigger_interrupt_0() { asm volatile("int $0"); }

void kernel_main(void) {
    terminal_clear();
    terminal_print("Initializing memory system...\n");
    k_memory_init();
    terminal_print("Memory system initialized.\n");

    terminal_print("Initializing IDT...\n");
    init_idt();
    terminal_print("IDT initialized.\n");

    terminal_print("Remapping PIC...\n");
    pic_remap(0x20, 0x28);
    terminal_print("PIC remapped.\n");

    terminal_print("Enabling interrupts...\n");
    asm volatile("sti");
    terminal_print("Interrupts enabled.\n");

    terminal_print("MiViE UNIX start\n");

    int count = 0;
    while (1) {
        terminal_putchar('.');
        for (volatile int i = 0; i < 10000000; i++) {
            // Busy wait loop to create a delay
        }
        __asm__("hlt");
    }
}
