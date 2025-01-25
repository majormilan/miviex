#include "vga.h"
#include "memory.h"

// Print a number in hexadecimal format
void terminal_print_num(uintptr_t num) {
    char buf[50];
    itoa(num, buf, 16);  // Convert the number to hex
    terminal_print(buf);  // Print the result
}

void kernel_main(void) {
    terminal_clear();

    // Initialize memory system
    k_memory_init();

    // Test malloc by allocating some memory
    void *ptr1 = k_malloc(131072);  // Allocate one page (4KB)
    if (ptr1 != NULL) {
        terminal_print_colorful("Allocated shitton of memory at: ", VGA_COLOR_LIGHT_GREEN);
        terminal_print_num((uintptr_t)ptr1);  // Print address in hex
        terminal_print("\n");
    } else {
        terminal_print_colorful("Failed to allocate 4KB of memory\n", VGA_COLOR_LIGHT_RED);
    }

    void *ptr2 = k_malloc(8192);  // Allocate two pages (8KB)
    if (ptr2 != NULL) {
        terminal_print_colorful("Allocated 8KB of memory at: ", VGA_COLOR_LIGHT_GREEN);
        terminal_print_num((uintptr_t)ptr2);  // Print address in hex
        terminal_print("\n");
    } else {
        terminal_print_colorful("Failed to allocate 8KB of memory\n", VGA_COLOR_LIGHT_RED);
    }

    // Test freeing memory
    k_free(ptr1);
    terminal_print_colorful("Freed 4KB of memory\n", VGA_COLOR_LIGHT_BLUE);

    k_free(ptr2);
    terminal_print_colorful("Freed 8KB of memory\n", VGA_COLOR_LIGHT_BLUE);

    // Hang the system to keep it running
    while (1) {
        __asm__("hlt");
    }
}
