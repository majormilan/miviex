#ifndef RAMFS_H
#define RAMFS_H

#include <kernel/vfs/vfs.h>

/* ramfs: everything in kernel memory. Files and symlinks keep their bytes
 * in a ramfs_file_data_t; directories are plain dentry lists. */
typedef struct ramfs_file_data {
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
} ramfs_file_data_t;

#define RAMFS_DEV 1

dentry_t* ramfs_init(void);

#endif // RAMFS_H
