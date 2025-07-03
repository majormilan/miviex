#include <kernel/vfs/ramfs.h>
#include <kernel/vfs/vfs.h>
#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>

// For now, we'll just have a single root node.
// In the future, we can add more complex structures.
inode_t *ramfs_root_inode;
dentry_t *ramfs_root_dentry;

// Implementation of the read function for the ramdisk
uint32_t ramfs_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    // Not implemented yet
    return 0;
}

// Implementation of the write function for the ramdisk
uint32_t ramfs_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    // Not implemented yet
    return 0;
}

// Implementation of the open function for the ramdisk
void ramfs_open(inode_t *node) {
    // Not implemented yet
}

// Implementation of the close function for the ramdisk
void ramfs_close(inode_t *node) {
    // Not implemented yet
}

// Function to initialize the ramdisk
dentry_t* ramfs_init() {
    terminal_print("RAMFS: Calling k_malloc for root inode...\n");
    ramfs_root_inode = (inode_t*)k_malloc(sizeof(inode_t));
    terminal_print("RAMFS: k_malloc returned ");
    terminal_print_num((uintptr_t)ramfs_root_inode);
    terminal_print("\n");
    terminal_print("RAMFS: sizeof(inode_t) = ");
    terminal_print_num(sizeof(inode_t));
    terminal_print("\n");
    terminal_print("RAMFS: alignof(inode_t) = ");
    terminal_print_num(__alignof__(inode_t));
    terminal_print("\n");
    terminal_print("RAMFS: Address of ramfs_root_inode->read = ");
    terminal_print_num((uintptr_t)&ramfs_root_inode->read);
    terminal_print("\n");

    // Zero out the allocated memory
    memset(ramfs_root_inode, 0, sizeof(inode_t));
    if (ramfs_root_inode == NULL) {
        terminal_print_colorful("RAMFS: Failed to allocate memory for root inode!\n", VGA_COLOR_LIGHT_RED);
        while(1);
    }
    ramfs_root_inode->name[0] = '/';
    ramfs_root_inode->name[1] = '\0';
    ramfs_root_inode->flags = VFS_DIRECTORY;
    ramfs_root_inode->read = ramfs_read;
    ramfs_root_inode->write = ramfs_write;
    ramfs_root_inode->open = ramfs_open;
    ramfs_root_inode->close = ramfs_close;

    terminal_print("RAMFS: Calling k_malloc for root dentry...\n");
    ramfs_root_dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    memset(ramfs_root_dentry, 0, sizeof(dentry_t));
    if (ramfs_root_dentry == NULL) {
        terminal_print_colorful("RAMFS: Failed to allocate memory for root dentry!\n", VGA_COLOR_LIGHT_RED);
        while(1);
    }
    strcpy(ramfs_root_dentry->name, "/");
    ramfs_root_dentry->inode = ramfs_root_inode;

    return ramfs_root_dentry;
}
