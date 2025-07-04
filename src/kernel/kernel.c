#include <kernel/drivers/keyboard.h>
#include <kernel/hal/idt.h>
#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>
#include <kernel/hal/pic.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>

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

void enable_interrupts() {
    asm volatile("sti");
}

void trigger_interrupt_0() { asm volatile("int $0"); }

extern unsigned char* __bss_start;
extern unsigned char* __bss_end;

void kernel_main(void) {
    terminal_clear();

    terminal_print_colorful("MiViE UNIX start\n", VGA_COLOR_LIGHT_BROWN);
    execute_and_report(WRAP(k_memory_init), "Initializing memory system");
    execute_and_report(WRAP(init_idt), "Initializing IDT");
    execute_and_report(WRAP(pic_remap, 0x20, 0x28), "Remapping PIC");
    execute_and_report(WRAP(enable_interrupts), "Enabling interrupts");
    execute_and_report(WRAP(keyboard_init), "Enable keyboard");
    dentry_t *ramfs_root = vfs_init();
    dentry_t *devfs_root = devfs_init();

    vfs_mount(ramfs_root, devfs_root);

    terminal_print("VFS Tree:\n");
    vfs_debug_print_tree(ramfs_root, 0);
    
    // VFS Test: Create, Write, Read a file
    terminal_print_colorful("\n--- VFS File Test ---\n", VGA_COLOR_CYAN);

    char *test_filename = "/test_file.txt";
    char *write_data = "Hello, RAMFS! This is a test string.";
    char read_buffer[100];
    memset(read_buffer, 0, sizeof(read_buffer));

    terminal_print("Attempting to create and open file: ");
    terminal_print(test_filename);
    terminal_print("\n");

    int fd = open(test_filename, O_CREAT | O_RDWR);
    if (fd >= 0) {
        terminal_print_colorful("File opened successfully (fd: ", VGA_COLOR_GREEN);
        terminal_print_num(fd);
        terminal_print_colorful(")\n", VGA_COLOR_GREEN);

        terminal_print("Attempting to write to file...\n");
        uint32_t bytes_written = write(fd, (uint8_t*)write_data, strlen(write_data));
        terminal_print_colorful("Bytes written: ", VGA_COLOR_GREEN);
        terminal_print_num(bytes_written);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);

        // Reset offset to read from beginning
        open_files[fd]->offset = 0; 

        terminal_print("Attempting to read from file...\n");
        uint32_t bytes_read = read(fd, (uint8_t*)read_buffer, sizeof(read_buffer) - 1);
        terminal_print_colorful("Bytes read: ", VGA_COLOR_GREEN);
        terminal_print_num(bytes_read);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);
        terminal_print_colorful("Read data: ", VGA_COLOR_GREEN);
        terminal_print_colorful(read_buffer, VGA_COLOR_GREEN);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);

        if (strcmp(write_data, read_buffer) == 0) {
            terminal_print_colorful("Data verification: SUCCESS!\n", VGA_COLOR_GREEN);
        } else {
            terminal_print_colorful("Data verification: FAILED!\n", VGA_COLOR_RED);
        }

        close(fd);
        terminal_print_colorful("File closed.\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("Failed to open/create file!\n", VGA_COLOR_RED);
    }

    terminal_print_colorful("--- VFS File Test Complete ---\n", VGA_COLOR_CYAN);

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
