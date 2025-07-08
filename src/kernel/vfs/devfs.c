#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>
#include <kernel/hal/io.h>
#include <kernel/vfs/ramfs.h>

// Placeholder device read/write functions
uint32_t dev_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    terminal_print("DEVFS: Reading from device ");
    terminal_print(node->name);
    terminal_print("\n");
    // For now, just return 0 bytes read
    return 0;
}

uint32_t dev_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    terminal_print("DEVFS: Writing to device ");
    terminal_print(node->name);
    terminal_print("\n");
    // For now, just return 0 bytes written
    return 0;
}

void dev_open(inode_t *node) {
    terminal_print("DEVFS: Opening device ");
    terminal_print(node->name);
    terminal_print("\n");
}

void dev_close(inode_t *node) {
    terminal_print("DEVFS: Closing device ");
    terminal_print(node->name);
    terminal_print("\n");
}

dentry_t* devfs_init() {
    terminal_print("DEVFS: Initializing device filesystem...\n");

    // Create a root inode for /dev
    inode_t *dev_root_inode = (inode_t*)k_malloc(sizeof(inode_t));
    dentry_t *dev_root_dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    if (dev_root_inode == NULL) {
        terminal_print_colorful("DEVFS: Failed to allocate memory for /dev inode!\n", VGA_COLOR_LIGHT_RED);
        while(1);
    }
    memset(dev_root_inode, 0, sizeof(inode_t));
    strcpy(dev_root_inode->name, "dev");
    dev_root_inode->flags = VFS_DIRECTORY;
    dev_root_inode->ptr = (void*)dev_root_dentry;
    dev_root_inode->stat = ramfs_stat;

    // Create a dentry for /dev
    if (dev_root_dentry == NULL) {        terminal_print_colorful("DEVFS: Failed to allocate memory for /dev dentry!\n", VGA_COLOR_LIGHT_RED);        while(1);    }    memset(dev_root_dentry, 0, sizeof(dentry_t));    strcpy(dev_root_dentry->name, "dev");    dev_root_dentry->inode = dev_root_inode;
    dev_root_dentry->parent = NULL;
    dev_root_dentry->first_child = NULL;
    dev_root_dentry->next_sibling = NULL;

    // For now, we'll just return the dentry.
    // In a real implementation, you'd add device nodes here.
    terminal_print("DEVFS: Device filesystem initialized.\n");
    return dev_root_dentry;
}
