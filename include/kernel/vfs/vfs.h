#ifndef VFS_H
#define VFS_H

#include <kernel/types.h>
#include <stdint.h>

// Inode types (flags & VFS_TYPE).
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_TYPE        0x07

#define NAME_MAX 127

struct inode;

// Bytes transferred, or -errno (-ERESTARTSYS when a signal interrupted a
// blocking read or write).
typedef long (*read_type_t)(struct inode*, uint32_t, uint32_t, uint8_t*);
typedef long (*write_type_t)(struct inode*, uint32_t, uint32_t, uint8_t*);
typedef struct inode* (*finddir_type_t)(struct inode*, char *name);
// Make a new inode of `type` named `name` in directory `dir`.
typedef struct inode* (*create_type_t)(struct inode *dir, const char *name,
                                       uint32_t type, uint32_t mode);
typedef int (*truncate_type_t)(struct inode*, uint32_t length);
typedef long (*ioctl_type_t)(struct inode*, unsigned long req, uint64_t arg);
typedef void (*release_type_t)(struct inode*, uint32_t flags);
typedef void (*destroy_type_t)(struct inode*);

// A file, directory, device, pipe or symlink. Directories keep their one
// dentry in ->ptr; symlinks keep their target as file data.
typedef struct inode {
    char name[NAME_MAX + 1];
    uint32_t flags;    // VFS_* type
    uint32_t inode;    // inode number
    uint32_t length;   // size in bytes
    uint32_t mode;     // permission bits, incl. setuid/setgid/sticky
    uint32_t uid, gid;
    uint32_t nlink;    // names (dentries) referring to it
    uint32_t refs;     // open file descriptions
    uint32_t dev;      // filesystem device number
    uint32_t rdev;     // device number of a device node
    uint64_t atime, mtime, ctime; // ns since the epoch
    read_type_t read;
    write_type_t write;
    finddir_type_t finddir;
    create_type_t create;
    truncate_type_t truncate;
    ioctl_type_t ioctl;
    release_type_t release; // last close of an open file description
    destroy_type_t destroy; // no names and no open files left
    void *ptr;              // filesystem-specific data
} inode_t;

typedef struct dentry {
    char name[NAME_MAX + 1];
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

dentry_t* vfs_init();
void vfs_add_child(dentry_t *parent, dentry_t *child);
void vfs_mount(dentry_t *mountpoint, dentry_t *root);

// Fresh inode metadata: type, mode, owner, timestamps, one link.
void vfs_init_inode(inode_t *n, uint32_t type, uint32_t mode);
// Set the given timestamps (VFS_A/M/CTIME) to now.
#define VFS_ATIME 1
#define VFS_MTIME 2
#define VFS_CTIME 4
void vfs_touch(inode_t *n, int which);
// finddir for filesystems whose directories are plain dentry lists.
inode_t *vfs_finddir_dentry(inode_t *dir, char *name);
dentry_t *vfs_child(inode_t *dir, const char *name);

// Path resolution from `base` (absolute paths start at "/"). Symlinks are
// followed, the last one only if `follow`. NULL with -errno in *err.
inode_t *vfs_resolve(inode_t *base, const char *path, bool follow, long *err);
inode_t *vfs_lookup(inode_t *base, char *path);
// Directory of the last path component, and that component in `name`.
inode_t *vfs_parent(inode_t *base, const char *path, char name[NAME_MAX + 1], long *err);

// Namespace operations on a directory and a name: 0 or -errno.
long vfs_create(inode_t *dir, const char *name, uint32_t type, uint32_t mode, inode_t **out);
long vfs_unlink(inode_t *dir, const char *name);
long vfs_rmdir(inode_t *dir, const char *name);
long vfs_link(inode_t *dir, const char *name, inode_t *target);
long vfs_rename(inode_t *odir, const char *oname, inode_t *ndir, const char *nname,
                bool noreplace);

// Open file descriptions (src/kernel/vfs/file.c). file_open returns a
// description with one reference, or NULL with -errno in *err.
file_t *file_open(inode_t *base, const char *path, uint32_t flags, uint32_t mode, long *err);
file_t *file_alloc(inode_t *node, uint32_t flags);
file_t *file_get(file_t *f);
void file_put(file_t *f);

// Anonymous pipe (src/kernel/vfs/pipe.c): read and write ends. 0 or -errno.
long pipe_create(file_t **rd, file_t **wr);

dentry_t* devfs_init();

// Debug function to print the VFS tree
void vfs_debug_print_tree(dentry_t *dentry, int level);


#endif // VFS_H
