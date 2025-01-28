#include "vga.h"
#include "io.h"
#include "keyboard.h"

void isr0_handler() {
    terminal_print_colorful("Interrupt 0 triggered!\n", VGA_COLOR_LIGHT_RED);
}

void isr32_handler() {
    outb(0x20, 0x20); // Send EOI to master PIC
}


void isr33_handler() {
    uint8_t scancode = inb(0x60); // Read scancode from keyboard controller
    char c = scancode_to_ascii(scancode);
    if (c != 0) {
        terminal_putchar(c); // Display the character
    }
    outb(0x20, 0x20); // Send End of Interrupt (EOI) signal to PIC
}
