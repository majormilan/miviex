// kernel.c - Main Kernel Entry Point

#include "vga.h"

void kernel_main(void) {
    terminal_clear();
    // Print colorful ASCII art
    terminal_print_colorful("MiViE UNIX\n", VGA_COLOR_LIGHT_BROWN);
    terminal_print("Initializing boot sequence...");
    // Hang the system
    while (1) {
        __asm__("hlt");
    }
}
