#include <kernel/vfs/initramfs.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>
#include <kernel/vfs/ramfs.h> // For ramfs_create, ramfs_mkdir

// CPIO new ASCII format header
typedef struct cpio_newc_header {
    char    c_magic[6];
    char    c_ino[8];
    char    c_mode[8];
    char    c_uid[8];
    char    c_gid[8];
    char    c_nlink[8];
    char    c_mtime[8];
    char    c_filesize[8];
    char    c_devmajor[8];
    char    c_devminor[8];
    char    c_rdevmajor[8];
    char    c_rdevminor[8];
    char    c_namesize[8];
    char    c_check[8];
} cpio_newc_header_t;

// Helper to convert hex string to uint32_t
static uint32_t hex_to_uint(const char *hex, int len) {
    uint32_t num = 0;
    for (int i = 0; i < len; i++) {
        char c = hex[i];
        if (c >= '0' && c <= '9') {
            num = (num << 4) | (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            num = (num << 4) | (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            num = (num << 4) | (c - 'A' + 10);
        }
    }
    return num;
}

// Function to parse the initramfs (CPIO archive)
void initramfs_parse(uint32_t start, uint32_t end) {
    terminal_print_colorful("INITRAMFS: Parsing initramfs from 0x", VGA_COLOR_LIGHT_BLUE);
    terminal_print_hex(start);
    terminal_print_colorful(" to 0x", VGA_COLOR_LIGHT_BLUE);
    terminal_print_hex(end);
    terminal_print_colorful("\n", VGA_COLOR_LIGHT_BLUE);

    uint32_t current_ptr = start;

    while (current_ptr < end) {
        cpio_newc_header_t *header = (cpio_newc_header_t *)current_ptr;

        // Check magic number
        if (strncmp(header->c_magic, "070701", 6) != 0) {
            terminal_print_colorful("INITRAMFS: Invalid CPIO magic! Aborting.\n", VGA_COLOR_RED);
            break;
        }

        uint32_t namesize = hex_to_uint(header->c_namesize, 8);
        uint32_t filesize = hex_to_uint(header->c_filesize, 8);
        uint32_t mode = hex_to_uint(header->c_mode, 8);

        char *filename = (char *)(current_ptr + sizeof(cpio_newc_header_t));

        // Align header + name to 4-byte boundary
        uint32_t header_and_name_size = sizeof(cpio_newc_header_t) + namesize;
        if (header_and_name_size % 4 != 0) {
            header_and_name_size += (4 - (header_and_name_size % 4));
        }

        uint8_t *file_data = (uint8_t *)(current_ptr + header_and_name_size);

        // Align file data to 4-byte boundary
        uint32_t file_data_size_aligned = filesize;
        if (file_data_size_aligned % 4 != 0) {
            file_data_size_aligned += (4 - (file_data_size_aligned % 4));
        }

        // Check for end of archive
        if (strcmp(filename, "TRAILER!!!") == 0) {
            terminal_print_colorful("INITRAMFS: End of CPIO archive.\n", VGA_COLOR_LIGHT_BLUE);
            break;
        }

        terminal_print_colorful("INITRAMFS: Found file: ", VGA_COLOR_LIGHT_CYAN);
        terminal_print(filename);
        terminal_print_colorful(" (size: ", VGA_COLOR_LIGHT_CYAN);
        terminal_print_num(filesize);
        terminal_print_colorful(" bytes, mode: 0x", VGA_COLOR_LIGHT_CYAN);
        terminal_print_hex(mode);
        terminal_print_colorful(")\n", VGA_COLOR_LIGHT_CYAN);

        // Determine file type and create in RAMFS
        inode_t *parent_node = vfs_get_root()->inode; // Assuming all initramfs files are relative to root for now

        // Handle directories
        if ((mode & 0xF000) == 0x4000) { // S_IFDIR (directory)
            // Need to handle nested directories. For simplicity, assume flat structure or create parents as needed.
            // For now, let's just try to create it directly under root.
            // A more robust solution would involve parsing the path and creating intermediate directories.
            ramfs_mkdir(parent_node, filename, mode);
        } else if ((mode & 0xF000) == 0x8000) { // S_IFREG (regular file)
            inode_t *new_file_node = ramfs_create(parent_node, filename, VFS_FILE);
            if (new_file_node) {
                ramfs_write(new_file_node, 0, filesize, file_data);
            } else {
                terminal_print_colorful("INITRAMFS: Failed to create file in RAMFS: ", VGA_COLOR_RED);
                terminal_print(filename);
                terminal_print("\n");
            }
        } else {
            terminal_print_colorful("INITRAMFS: Unsupported file type (mode: 0x", VGA_COLOR_YELLOW);
            terminal_print_hex(mode);
            terminal_print_colorful(") for file: ", VGA_COLOR_YELLOW);
            terminal_print(filename);
            terminal_print("\n");
        }

        current_ptr += header_and_name_size + file_data_size_aligned;
    }
}