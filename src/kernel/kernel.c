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

        // Test lseek
        terminal_print_colorful("\n--- lseek Test ---\n", VGA_COLOR_CYAN);
        char *lseek_test_data = "abcdefghijklmnopqrstuvwxyz";
        char lseek_read_buffer[10];
        memset(lseek_read_buffer, 0, sizeof(lseek_read_buffer));

        // Write data to a new file for lseek test
        int lseek_fd = open("/lseek_test.txt", O_CREAT | O_RDWR);
        if (lseek_fd >= 0) {
            write(lseek_fd, (uint8_t*)lseek_test_data, strlen(lseek_test_data));
            terminal_print_colorful("lseek test: Wrote data to /lseek_test.txt\n", VGA_COLOR_GREEN);

            // Seek to offset 5 from beginning
            int new_offset = lseek(lseek_fd, 5, SEEK_SET);
            terminal_print_colorful("lseek test: Seeked to offset ", VGA_COLOR_GREEN);
            terminal_print_num(new_offset);
            terminal_print_colorful(" (SEEK_SET)\n", VGA_COLOR_GREEN);

            // Read 5 bytes from new offset
            read(lseek_fd, (uint8_t*)lseek_read_buffer, 5);
            terminal_print_colorful("lseek test: Read data: ", VGA_COLOR_GREEN);
            terminal_print_colorful(lseek_read_buffer, VGA_COLOR_GREEN);
            terminal_print_colorful("\n", VGA_COLOR_GREEN);

            if (strcmp(lseek_read_buffer, "fghij") == 0) {
                terminal_print_colorful("lseek test: Read data verification: SUCCESS!\n", VGA_COLOR_GREEN);
            } else {
                terminal_print_colorful("lseek test: Read data verification: FAILED!\n", VGA_COLOR_RED);
            }

            // Seek relative to current position
            new_offset = lseek(lseek_fd, 2, SEEK_CUR);
            terminal_print_colorful("lseek test: Seeked to offset ", VGA_COLOR_GREEN);
            terminal_print_num(new_offset);
            terminal_print_colorful(" (SEEK_CUR)\n", VGA_COLOR_GREEN);

            // Read 3 bytes
            memset(lseek_read_buffer, 0, sizeof(lseek_read_buffer));
            read(lseek_fd, (uint8_t*)lseek_read_buffer, 3);
            terminal_print_colorful("lseek test: Read data: ", VGA_COLOR_GREEN);
            terminal_print_colorful(lseek_read_buffer, VGA_COLOR_GREEN);
            terminal_print_colorful("\n", VGA_COLOR_GREEN);

            if (strcmp(lseek_read_buffer, "mno") == 0) {
                terminal_print_colorful("lseek test: Read data verification: SUCCESS!\n", VGA_COLOR_GREEN);
            } else {
                terminal_print_colorful("lseek test: Read data verification: FAILED!\n", VGA_COLOR_RED);
            }

            // Seek from end
            new_offset = lseek(lseek_fd, -5, SEEK_END);
            terminal_print_colorful("lseek test: Seeked to offset ", VGA_COLOR_GREEN);
            terminal_print_num(new_offset);
            terminal_print_colorful(" (SEEK_END)\n", VGA_COLOR_GREEN);

            // Read 5 bytes
            memset(lseek_read_buffer, 0, sizeof(lseek_read_buffer));
            read(lseek_fd, (uint8_t*)lseek_read_buffer, 5);
            terminal_print_colorful("lseek test: Read data: ", VGA_COLOR_GREEN);
            terminal_print_colorful(lseek_read_buffer, VGA_COLOR_GREEN);
            terminal_print_colorful("\n", VGA_COLOR_GREEN);

            if (strcmp(lseek_read_buffer, "vwxyz") == 0) {
                terminal_print_colorful("lseek test: Read data verification: SUCCESS!\n", VGA_COLOR_GREEN);
            } else {
                terminal_print_colorful("lseek test: Read data verification: FAILED!\n", VGA_COLOR_RED);
            }

            close(lseek_fd);
            terminal_print_colorful("lseek test: File closed.\n", VGA_COLOR_GREEN);
        } else {
            terminal_print_colorful("lseek test: Failed to open/create /lseek_test.txt!\n", VGA_COLOR_RED);
        }
        terminal_print_colorful("--- lseek Test Complete ---\n", VGA_COLOR_CYAN);

        close(fd);
        terminal_print_colorful("File closed.\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("Failed to open/create file!\n", VGA_COLOR_RED);
    }

    // Test mkdir and rmdir
    terminal_print_colorful("\n--- mkdir/rmdir Test ---\n", VGA_COLOR_CYAN);
    char *test_dir = "/test_dir";

    terminal_print("Attempting to create directory: ");
    terminal_print(test_dir);
    terminal_print("\n");
    if (mkdir(test_dir, 0) == 0) {
        terminal_print_colorful("Directory created successfully!\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("Failed to create directory!\n", VGA_COLOR_RED);
    }
    terminal_print("VFS Tree after mkdir:\n");
    vfs_debug_print_tree(ramfs_root, 0);

    terminal_print("Attempting to remove directory: ");
    terminal_print(test_dir);
    terminal_print("\n");
    if (rmdir(test_dir) == 0) {
        terminal_print_colorful("Directory removed successfully!\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("Failed to remove directory!\n", VGA_COLOR_RED);
    }
    terminal_print("VFS Tree after rmdir:\n");
    vfs_debug_print_tree(ramfs_root, 0);
    terminal_print_colorful("--- mkdir/rmdir Test Complete ---\n", VGA_COLOR_CYAN);

    // Test unlink
    terminal_print_colorful("\n--- unlink Test ---\n", VGA_COLOR_CYAN);
    char *unlink_test_file = "/unlink_test.txt";

    terminal_print("Attempting to create file for unlink: ");
    terminal_print(unlink_test_file);
    terminal_print("\n");
    int unlink_fd = open(unlink_test_file, O_CREAT | O_RDWR);
    if (unlink_fd >= 0) {
        terminal_print_colorful("File created successfully for unlink!\n", VGA_COLOR_GREEN);
        close(unlink_fd);
    } else {
        terminal_print_colorful("Failed to create file for unlink!\n", VGA_COLOR_RED);
    }
    terminal_print("VFS Tree after file creation for unlink:\n");
    vfs_debug_print_tree(ramfs_root, 0);

    terminal_print("Attempting to unlink file: ");
    terminal_print(unlink_test_file);
    terminal_print("\n");
    if (unlink(unlink_test_file) == 0) {
        terminal_print_colorful("File unlinked successfully!\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("Failed to unlink file!\n", VGA_COLOR_RED);
    }
    terminal_print("VFS Tree after unlink:\n");
    vfs_debug_print_tree(ramfs_root, 0);
    terminal_print_colorful("--- unlink Test Complete ---\n", VGA_COLOR_CYAN);

    // Test stat
    terminal_print_colorful("\n--- stat Test ---\n", VGA_COLOR_CYAN);
    char *stat_test_file = "/test_file.txt";
    stat_t file_stat;
    memset(&file_stat, 0, sizeof(stat_t));

    terminal_print("Attempting to stat file: ");
    terminal_print(stat_test_file);
    terminal_print("\n");
    if (stat(stat_test_file, &file_stat) == 0) {
        terminal_print_colorful("Stat successful!\n", VGA_COLOR_GREEN);
        terminal_print_colorful("  Inode: ", VGA_COLOR_GREEN);
        terminal_print_num(file_stat.st_ino);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);
        terminal_print_colorful("  Size: ", VGA_COLOR_GREEN);
        terminal_print_num(file_stat.st_size);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);
        terminal_print_colorful("  Mode: ", VGA_COLOR_GREEN);
        terminal_print_num(file_stat.st_mode);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("Failed to stat file!\n", VGA_COLOR_RED);
    }

    char *stat_test_dir = "/dev";
    memset(&file_stat, 0, sizeof(stat_t));

    terminal_print("Attempting to stat directory: ");
    terminal_print(stat_test_dir);
    terminal_print("\n");
    if (stat(stat_test_dir, &file_stat) == 0) {
        terminal_print_colorful("Stat successful!\n", VGA_COLOR_GREEN);
        terminal_print_colorful("  Inode: ", VGA_COLOR_GREEN);
        terminal_print_num(file_stat.st_ino);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);
        terminal_print_colorful("  Size: ", VGA_COLOR_GREEN);
        terminal_print_num(file_stat.st_size);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);
        terminal_print_colorful("  Mode: ", VGA_COLOR_GREEN);
        terminal_print_num(file_stat.st_mode);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("Failed to stat directory!\n", VGA_COLOR_RED);
    }
    terminal_print_colorful("--- stat Test Complete ---\n", VGA_COLOR_CYAN);

    // Test readdir
    terminal_print_colorful("\n--- readdir Test ---\n", VGA_COLOR_CYAN);
    char *readdir_test_dir = "/readdir_test_dir";
    char *file1 = "/readdir_test_dir/file1.txt";
    char *file2 = "/readdir_test_dir/file2.txt";

    if (mkdir(readdir_test_dir, 0) == 0) {
        terminal_print_colorful("Created directory: ", VGA_COLOR_GREEN);
        terminal_print_colorful(readdir_test_dir, VGA_COLOR_GREEN);
        terminal_print_colorful("\n", VGA_COLOR_GREEN);

        int fd1 = open(file1, O_CREAT | O_RDWR);
        if (fd1 >= 0) { close(fd1); terminal_print_colorful("Created file: ", VGA_COLOR_GREEN); terminal_print_colorful(file1, VGA_COLOR_GREEN); terminal_print_colorful("\n", VGA_COLOR_GREEN); }
        else { terminal_print_colorful("Failed to create file: ", VGA_COLOR_RED); terminal_print_colorful(file1, VGA_COLOR_RED); terminal_print_colorful("\n", VGA_COLOR_RED); }

        int fd2 = open(file2, O_CREAT | O_RDWR);
        if (fd2 >= 0) { close(fd2); terminal_print_colorful("Created file: ", VGA_COLOR_GREEN); terminal_print_colorful(file2, VGA_COLOR_GREEN); terminal_print_colorful("\n", VGA_COLOR_GREEN); }
        else { terminal_print_colorful("Failed to create file: ", VGA_COLOR_RED); terminal_print_colorful(file2, VGA_COLOR_RED); terminal_print_colorful("\n", VGA_COLOR_RED); }

        terminal_print("Reading directory entries for ");
        terminal_print(readdir_test_dir);
        terminal_print(":\n");

        inode_t *dir_node = vfs_lookup(fs_root->inode, readdir_test_dir);
        if (dir_node != NULL && (dir_node->flags & VFS_DIRECTORY)) {
            struct dirent *entry;
            uint32_t index = 0;
            while ((entry = dir_node->readdir(dir_node, index)) != NULL) {
                terminal_print_colorful("  Entry: ", VGA_COLOR_GREEN);
                terminal_print_colorful(entry->name, VGA_COLOR_GREEN);
                terminal_print_colorful("\n", VGA_COLOR_GREEN);
                k_free(entry); // Free the dirent structure allocated by readdir
                index++;
            }
        } else {
            terminal_print_colorful("Failed to lookup directory or not a directory!\n", VGA_COLOR_RED);
        }

        // Clean up
        unlink(file1);
        unlink(file2);
        rmdir(readdir_test_dir);
        terminal_print_colorful("Cleaned up readdir test files and directory.\n", VGA_COLOR_GREEN);

    } else {
        terminal_print_colorful("Failed to create directory for readdir test!\n", VGA_COLOR_RED);
    }
    terminal_print_colorful("--- readdir Test Complete ---\n", VGA_COLOR_CYAN);

    terminal_print("VFS Tree:\n");
    vfs_debug_print_tree(ramfs_root, 0);

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
