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

#define RAMFS_DEV 1 /* the root filesystem; tmpfs mounts get their own */

/* A new, empty instance: its root directory's dentry. */
dentry_t* ramfs_init(uint32_t dev, uint32_t mode);

#endif // RAMFS_H
