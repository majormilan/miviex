#include "vga.h"
#include "io.h"
#include "keyboard.h"
#include "types.h"

void isr0_handler() {
    terminal_print_colorful("Interrupt 0 triggered!\n", VGA_COLOR_LIGHT_RED);
}

void isr32_handler() {
    outb(0x20, 0x20); // Send EOI to master PIC
}

void isr33_handler() {
    keyboard_isr();
}
