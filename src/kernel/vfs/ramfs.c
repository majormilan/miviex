#include <kernel/vfs/ramfs.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>

// For now, we'll just have a single root node.
// In the future, we can add more complex structures.
inode_t *ramfs_root_inode;
dentry_t *ramfs_root_dentry;

// Implementation of the read function for the ramdisk
uint32_t ramfs_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!(node->flags & VFS_FILE)) {
        return 0; // Not a file
    }
    ramfs_file_data_t *file_data = (ramfs_file_data_t *)node->ptr;
    if (file_data == NULL || offset >= file_data->size) {
        return 0; // Invalid offset or no data
    }
    uint32_t bytes_to_read = size;
    if (offset + size > file_data->size) {
        bytes_to_read = file_data->size - offset;
    }
    memcpy(buffer, file_data->data + offset, bytes_to_read);
    return bytes_to_read;
}

// Implementation of the write function for the ramdisk
uint32_t ramfs_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    if (!(node->flags & VFS_FILE)) {
        return 0; // Not a file
    }
    ramfs_file_data_t *file_data = (ramfs_file_data_t *)node->ptr;
    if (file_data == NULL) {
        // Initial allocation for file data
        file_data = (ramfs_file_data_t *)k_malloc(sizeof(ramfs_file_data_t));
        if (file_data == NULL) return 0;
        memset(file_data, 0, sizeof(ramfs_file_data_t));
        file_data->capacity = PAGE_SIZE; // Start with one page capacity
        file_data->data = (uint8_t *)k_malloc(file_data->capacity);
        if (file_data->data == NULL) {
            k_free(file_data);
            return 0;
        }
        node->ptr = (void*)file_data;
    }

    // Expand buffer if necessary
    if (offset + size > file_data->capacity) {
        uint32_t new_capacity = file_data->capacity;
        while (new_capacity < offset + size) {
            new_capacity += PAGE_SIZE;
        }
        uint8_t *new_data = (uint8_t *)k_malloc(new_capacity);
        if (new_data == NULL) return 0;
        memcpy(new_data, file_data->data, file_data->size);
        k_free(file_data->data);
        file_data->data = new_data;
        file_data->capacity = new_capacity;
    }

    memcpy(file_data->data + offset, buffer, size);
    if (offset + size > file_data->size) {
        file_data->size = offset + size;
    }
    node->length = file_data->size;
    return size;
}

// Implementation of the open function for the ramdisk
void ramfs_open(inode_t *node) {
    // Not implemented yet
}

// Implementation of the close function for the ramdisk
void ramfs_close(inode_t *node) {
    // Not implemented yet
}

inode_t* ramfs_create(inode_t *parent, char *name, uint32_t flags) {
    if (!(parent->flags & VFS_DIRECTORY)) {
        return NULL; // Parent is not a directory
    }

    inode_t *new_inode = (inode_t*)k_malloc(sizeof(inode_t));
    dentry_t *new_dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    if (new_inode == NULL) {
        return NULL;
    }
    memset(new_inode, 0, sizeof(inode_t));
    strcpy(new_inode->name, name);
    new_inode->flags = flags;
    new_inode->read = ramfs_read;
    new_inode->write = ramfs_write;
    new_inode->open = ramfs_open;
    new_inode->close = ramfs_close;

    if (flags & VFS_DIRECTORY) {
        new_inode->ptr = (void*)new_dentry;
    }

    if (new_dentry == NULL) {
        k_free(new_inode);
        return NULL;
    }
    memset(new_dentry, 0, sizeof(dentry_t));
    strcpy(new_dentry->name, name);
    new_dentry->inode = new_inode;

    // Link dentry to parent
    vfs_mount((dentry_t*)parent->ptr, new_dentry); // parent->ptr holds the dentry for ramfs

    return new_inode;
}

inode_t* ramfs_finddir(inode_t *node, char *name) {
    if (!(node->flags & VFS_DIRECTORY)) {
        return NULL;
    }
    dentry_t *parent_dentry = (dentry_t *)node->ptr;
    dentry_t *child = parent_dentry->first_child;
    while (child != NULL) {
        if (strcmp(child->name, name) == 0) {
            return child->inode;
        }
        child = child->next_sibling;
    }
    return NULL;
}

dentry_t* ramfs_init() {
    ramfs_root_dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    memset(ramfs_root_dentry, 0, sizeof(dentry_t));
    if (ramfs_root_dentry == NULL) {
        terminal_print_colorful("RAMFS: Failed to allocate memory for root dentry!\n", VGA_COLOR_LIGHT_RED);
        while(1);
    }
    strcpy(ramfs_root_dentry->name, "/");

    ramfs_root_inode = (inode_t*)k_malloc(sizeof(inode_t));
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
    ramfs_root_inode->ptr = (void*)ramfs_root_dentry;
    ramfs_root_inode->create = ramfs_create;
    ramfs_root_inode->finddir = ramfs_finddir;

    ramfs_root_dentry->inode = ramfs_root_inode;

    return ramfs_root_dentry;
}
