#ifndef RAMFS_H
#define RAMFS_H

#include <kernel/vfs/vfs.h>

typedef struct ramfs_file_data {
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
} ramfs_file_data_t;

// Function to initialize the ramdisk and mount it as the root filesystem
dentry_t* ramfs_init();
uint32_t ramfs_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t ramfs_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
inode_t* ramfs_create(inode_t *parent, char *name, uint32_t flags);

#endif // RAMFS_H
