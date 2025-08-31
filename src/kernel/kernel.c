#include <kernel/drivers/keyboard.h>
#include <kernel/hal/idt.h>
#include <kernel/hal/gdt.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <kernel/video/vga.h>
#include <kernel/hal/pic.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/hal/multiboot.h>
#include <kernel/syscall/syscall.h>
#include <kernel/vfs/initramfs.h> // Include for initramfs_parse

extern uint32_t initramfs_start; // Declare global initramfs_start
extern uint32_t initramfs_end;   // Declare global initramfs_end


#define O_CREAT 0x01
#define O_RDWR  0x02

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

int enable_interrupts() {
    asm volatile("sti");
    return 0;
}

void trigger_interrupt_0() { asm volatile("int $0"); }

extern unsigned char* __bss_start;
extern unsigned char* __bss_end;




extern void kernel_main(void) {
    terminal_clear();
    uint32_t ebx_at_start = *(uint32_t*)0x8004;
    terminal_print_colorful("MiViE UNIX starts\n", VGA_COLOR_LIGHT_BROWN);
    parse_multiboot_info((uint64_t*)(uint64_t)ebx_at_start);


    execute_and_report(gdt_init, "Initializing GDT");
    execute_and_report(k_memory_init, "Initializing memory system");

    address_space_t* kernel_address_space = vmm_create_address_space();
    vmm_switch_address_space(kernel_address_space);

    execute_and_report(init_idt, "Initializing IDT");
    execute_and_report(init_syscalls, "Initializing syscalls");
    execute_and_report(pic_remap_wrapper, "Remapping PIC");
    execute_and_report(enable_interrupts, "Enabling interrupts");
    execute_and_report(keyboard_init, "Enable keyboard");
    dentry_t *fs_root = vfs_init();

    // Parse initramfs if found
    if (initramfs_start != 0 && initramfs_end != 0) {
        initramfs_parse(initramfs_start, initramfs_end);
    } else {
        terminal_print_colorful("INITRAMFS: No initramfs found.\n", VGA_COLOR_YELLOW);
    }

    while (1) {
	char c;
        if (keyboard_get_char(&c)) {
            if (c == '\b') {
                terminal_backspace();
            } else {
                terminal_putchar(c);
            }
        }

        __asm__("hlt");
    }
}

