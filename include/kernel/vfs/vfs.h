#ifndef VFS_H
#define VFS_H

#include <kernel/types.h>

#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08

#define MAX_OPEN_FILES 32 // Maximum number of open files

#define O_CREAT 0x01
#define O_RDWR  0x02

struct inode;

// Function pointer types for VFS operations
typedef uint32_t (*read_type_t)(struct inode*, uint32_t, uint32_t, uint8_t*);
typedef uint32_t (*write_type_t)(struct inode*, uint32_t, uint32_t, uint8_t*);
typedef void (*open_type_t)(struct inode*);
typedef void (*close_type_t)(struct inode*);
typedef struct dirent* (*readdir_type_t)(struct inode*, uint32_t);
typedef struct inode* (*finddir_type_t)(struct inode*, char *name);
typedef struct inode* (*create_type_t)(struct inode*, char *name, uint32_t flags);
typedef struct inode* (*lookup_type_t)(struct inode*, char *path);

// Represents a file or directory in the VFS
typedef struct inode {
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
    create_type_t create;
    lookup_type_t lookup;
    void *ptr; // Used for filesystem-specific data
} __attribute__((packed)) inode_t;

// Structure for directory entries
struct dirent {
    char name[128];
    uint32_t ino;
};

typedef struct dentry {
    char name[128];
    inode_t *inode;
    struct dentry *parent;
    struct dentry *first_child;
    struct dentry *next_sibling;
} dentry_t;

typedef struct file {
    inode_t *inode; // Pointer to the inode of the open file
    uint32_t offset; // Current read/write offset
    uint32_t flags;  // Open flags (e.g., O_RDONLY, O_WRONLY)
    uint32_t ref_count; // Reference count for the file
} file_t;

extern dentry_t *fs_root; // The root of the filesystem
extern file_t *open_files[MAX_OPEN_FILES]; // Global file descriptor table

// Standard VFS functions
uint32_t vfs_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t vfs_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void vfs_open(inode_t *node);
void vfs_close(inode_t *node);
struct dirent *vfs_readdir(inode_t *node, uint32_t index);
inode_t *vfs_finddir(inode_t *node, char *name);

dentry_t* vfs_init();
void vfs_mount(dentry_t *parent, dentry_t *child);
inode_t* vfs_create(inode_t *parent, char *name, uint32_t flags);
inode_t* vfs_lookup(inode_t *parent, char *path);

// New system call functions
int open(char *path, uint32_t flags);
int close(int fd);
uint32_t read(int fd, uint8_t *buf, uint32_t count);
uint32_t write(int fd, uint8_t *buf, uint32_t count);

dentry_t* devfs_init();

// Debug function to print the VFS tree
void vfs_debug_print_tree(dentry_t *dentry, int level);

#endif // VFS_H
