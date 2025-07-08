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
int ramfs_mkdir(inode_t *parent, char *name, uint32_t mode);
int ramfs_rmdir(inode_t *parent, char *name);
int ramfs_unlink(inode_t *parent, char *name);
int ramfs_stat(inode_t *node, stat_t *buf);
struct dirent* ramfs_readdir(inode_t *node, uint32_t index);
inode_t* ramfs_finddir(inode_t *node, char *name);

#endif // RAMFS_H
