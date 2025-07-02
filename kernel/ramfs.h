#ifndef RAMFS_H
#define RAMFS_H

#include "vfs.h"

// Function to initialize the ramdisk and mount it as the root filesystem
vfs_node_t* ramfs_init();

#endif // RAMFS_H
