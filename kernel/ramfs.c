#include "ramfs.h"
#include "vfs.h"
#include "memory.h"
#include "vga.h"

// For now, we'll just have a single root node.
// In the future, we can add more complex structures.
vfs_node_t *ramfs_root_node;

// Implementation of the read function for the ramdisk
uint32_t ramfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    // Not implemented yet
    return 0;
}

// Implementation of the write function for the ramdisk
uint32_t ramfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    // Not implemented yet
    return 0;
}

// Implementation of the open function for the ramdisk
void ramfs_open(vfs_node_t *node) {
    // Not implemented yet
}

// Implementation of the close function for the ramdisk
void ramfs_close(vfs_node_t *node) {
    // Not implemented yet
}

// Function to initialize the ramdisk
vfs_node_t* ramfs_init() {
    terminal_print("RAMFS: Calling k_malloc for root node...\n");
    ramfs_root_node = (vfs_node_t*)k_malloc(sizeof(vfs_node_t));
    terminal_print("RAMFS: k_malloc returned ");
    terminal_print_num((uintptr_t)ramfs_root_node);
    terminal_print("\n");
    terminal_print("RAMFS: sizeof(vfs_node_t) = ");
    terminal_print_num(sizeof(vfs_node_t));
    terminal_print("\n");
    terminal_print("RAMFS: alignof(vfs_node_t) = ");
    terminal_print_num(__alignof__(vfs_node_t));
    terminal_print("\n");
    terminal_print("RAMFS: Address of ramfs_root_node->read = ");
    terminal_print_num((uintptr_t)&ramfs_root_node->read);
    terminal_print("\n");

    // Zero out the allocated memory
    memset(ramfs_root_node, 0, sizeof(vfs_node_t));
    if (ramfs_root_node == NULL) {
        terminal_print_colorful("RAMFS: Failed to allocate memory for root node!\n", VGA_COLOR_LIGHT_RED);
        while(1);
    }
    ramfs_root_node->name[0] = '/';
    ramfs_root_node->name[1] = '\0';
    ramfs_root_node->flags = VFS_DIRECTORY;
    ramfs_root_node->read = ramfs_read;
    ramfs_root_node->write = ramfs_write;
    ramfs_root_node->open = ramfs_open;
    ramfs_root_node->close = ramfs_close;
    // ramfs_root_node->finddir = 0; // Not implemented yet;
    // ramfs_root_node->readdir = 0; // Not implemented yet;

    return ramfs_root_node;
}
