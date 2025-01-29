#include "keyboard.h"
#include "idt.h"
#include "memory.h"
#include "vga.h"
#include "pic.h"

#define COLOR_OK VGA_COLOR_LIGHT_GREEN
#define COLOR_FAILED VGA_COLOR_LIGHT_RED

#define WRAP(func, ...) ({ \
    int wrapped_func(void) { \
        func(__VA_ARGS__); \
        return 0; \
    } \
    wrapped_func; \
})

void terminal_print_status(const char *status, unsigned char color) {
    terminal_print_colorful(status, color);
}

typedef int (*init_func_t)(void);

int execute_and_report(init_func_t func, const char *message) {
    int status = func();

    if (status == 0) {
        terminal_print_status("[  OK  ] ", COLOR_OK);
    } else {
        terminal_print_status("[FAILED] ", COLOR_FAILED);
    }

    terminal_print(message);
    terminal_print("\n");
    return status;
}

void enable_interrupts() {
    asm volatile("sti");
}

void trigger_interrupt_0() { asm volatile("int $0"); }

void kernel_main(void) {
    terminal_clear();
    terminal_print_colorful("MiViE UNIX start\n", VGA_COLOR_LIGHT_BROWN);
    execute_and_report(WRAP(k_memory_init), "Initializing memory system");
    execute_and_report(WRAP(init_idt), "Initializing IDT");
    execute_and_report(WRAP(pic_remap, 0x20, 0x28), "Remapping PIC");
    execute_and_report(WRAP(enable_interrupts), "Enabling interrupts");
    execute_and_report(WRAP(keyboard_init), "Enable keyboard");
    

    while (1) {
	char c;
        if (keyboard_get_char(&c)) {
            terminal_putchar(c);
        }

        __asm__("hlt");
    }
}
