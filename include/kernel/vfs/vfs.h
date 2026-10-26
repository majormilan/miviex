#ifndef VFS_H
#define VFS_H

#include <kernel/types.h>
#include <stdint.h>

#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08


// Structure for stat system call
typedef struct stat {
    uint32_t st_ino;    // Inode number
    uint32_t st_size;   // Size of file in bytes
    uint32_t st_mode;   // File type and mode
    uint32_t st_nlink;  // Number of hard links
} stat_t;

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
typedef int (*truncate_type_t)(struct inode*, uint32_t length);
typedef long (*ioctl_type_t)(struct inode*, unsigned long req, uint64_t arg);

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
    int (*mkdir)(struct inode *parent, char *name, uint32_t mode);
    int (*rmdir)(struct inode *parent, char *name);
    int (*unlink)(struct inode *parent, char *name);
    int (*stat)(struct inode *node, struct stat *buf);
    truncate_type_t truncate;
    ioctl_type_t ioctl;
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

// An open file description, shared by dup()'d descriptors.
typedef struct file {
    inode_t *inode;
    uint64_t offset;
    uint32_t flags;  // Linux O_* flags given to open
    uint32_t refs;
} file_t;

extern dentry_t *fs_root; // The root of the filesystem

// Standard VFS functions
uint32_t vfs_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
uint32_t vfs_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
void vfs_open(inode_t *node);
void vfs_close(inode_t *node);
struct dirent *vfs_readdir(inode_t *node, uint32_t index);
inode_t *vfs_finddir(inode_t *node, char *name);

dentry_t* vfs_init();
void vfs_add_child(dentry_t *parent, dentry_t *child);
void vfs_mount(dentry_t *mountpoint, dentry_t *root);
inode_t* vfs_create(inode_t *parent, char *name, uint32_t flags);
inode_t* vfs_lookup(inode_t *parent, char *path);

// Open file descriptions (src/kernel/vfs/file.c). file_open returns a
// description with one reference, or NULL with -errno in *err.
file_t *file_open(inode_t *base, const char *path, uint32_t flags, long *err);
file_t *file_get(file_t *f);
void file_put(file_t *f);

// Seek constants
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// New system calls for directory management
int mkdir(char *path, uint32_t mode);
int rmdir(char *path);
int unlink(char *path);
int stat(char *path, stat_t *buf);

dentry_t* devfs_init();

// Debug function to print the VFS tree
void vfs_debug_print_tree(dentry_t *dentry, int level);

// Function to get the global VFS root
dentry_t* vfs_get_root();

#endif // VFS_H
