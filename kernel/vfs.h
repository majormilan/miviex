#ifndef VFS_H
#define VFS_H

#include "types.h"

#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08

struct vfs_node;

// Function pointer types for VFS operations
typedef uint32_t (*read_type_t)(struct vfs_node*, uint32_t, uint32_t, uint8_t*);
typedef uint32_t (*write_type_t)(struct vfs_node*, uint32_t, uint32_t, uint8_t*);
typedef void (*open_type_t)(struct vfs_node*);
typedef void (*close_type_t)(struct vfs_node*);
typedef struct dirent* (*readdir_type_t)(struct vfs_node*, uint32_t);
typedef struct vfs_node* (*finddir_type_t)(struct vfs_node*, char *name);

// Represents a file or directory in the VFS
typedef struct vfs_node {
    char name[128];
    uint32_t flags;
    uint32_t inode;
    uint32_t length;
    uint32_t impl;
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    struct vfs_node *ptr; // Used for mountpoints
} __attribute__((packed)) vfs_node_t;

// Structure for directory entries
struct dirent {
    char name[128];
    uint32_t ino;
};

extern vfs_node_t *fs_root; // The root of the filesystem

// Standard VFS functions
uint32_t vfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t vfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void vfs_open(vfs_node_t *node);
void vfs_close(vfs_node_t *node);
struct dirent *vfs_readdir(vfs_node_t *node, uint32_t index);
vfs_node_t *vfs_finddir(vfs_node_t *node, char *name);

void vfs_init();

#endif // VFS_H
