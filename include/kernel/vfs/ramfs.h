#ifndef RAMFS_H
#define RAMFS_H

#include <kernel/vfs/vfs.h>

// Function to initialize the ramdisk and mount it as the root filesystem
dentry_t* ramfs_init();

#endif // RAMFS_H
