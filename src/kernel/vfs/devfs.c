#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/hal/io.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/drivers/tty.h>
#include <kernel/proc/scheduler.h>
#include <kernel/mm/vmm.h>
#include <kernel/errno.h>

static uint32_t next_devfs_inode = 1;

// Terminals: the inode's ptr is the tty; /dev/tty (ptr NULL) is the
// caller's controlling terminal, or the console if it has none.
static tty_t *dev_tty(inode_t *node) {
    if (node->ptr != NULL) {
        return (tty_t *)node->ptr;
    }
    process_t *p = scheduler_current();
    return p->ctty != NULL ? p->ctty : tty_console;
}

static long tty_dev_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    return tty_read(dev_tty(node), buffer, size);
}

static long tty_dev_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    return tty_write(dev_tty(node), buffer, size);
}

static long tty_dev_ioctl(inode_t *node, unsigned long req, uint64_t arg) {
    return tty_ioctl(dev_tty(node), req, arg);
}

// /dev/null: always empty, swallows everything.
static long null_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static long null_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

// /dev/zero: endless zero bytes.
static long zero_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

void dev_open(inode_t *node) {
    (void)node;
}

void dev_close(inode_t *node) {
    (void)node;
}

int devfs_stat(inode_t *node, stat_t *buf) {
    if (node == NULL || buf == NULL) {
        return -1; // Invalid arguments
    }

    buf->st_ino = node->inode; // Inode number
    buf->st_mode = node->flags; // File type and permissions
    buf->st_nlink = 1; // For now, assume 1 hard link
    buf->st_size = node->length; // Size of file

    return 0;
}

// Allocates and links a new character-device inode/dentry as a child of
// dev_root, wired up with the given read/write implementations. Mirrors
// ramfs_mkdir()'s inode+dentry allocation/linking pattern.
static inode_t* devfs_add_device(dentry_t *dev_root, const char *name, read_type_t read_fn, write_type_t write_fn, ioctl_type_t ioctl_fn, void *ptr) {
    inode_t *node = (inode_t*)k_malloc(sizeof(inode_t));
    dentry_t *dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    if (node == NULL || dentry == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate device node '%s'", name);
        k_free(node);
        k_free(dentry);
        return NULL;
    }

    memset(node, 0, sizeof(inode_t));
    strcpy(node->name, name);
    node->flags = VFS_CHARDEVICE;
    node->inode = next_devfs_inode++;
    node->read = read_fn;
    node->write = write_fn;
    node->ioctl = ioctl_fn;
    node->ptr = ptr;
    node->open = dev_open;
    node->close = dev_close;
    node->stat = devfs_stat;

    memset(dentry, 0, sizeof(dentry_t));
    strcpy(dentry->name, name);
    dentry->inode = node;
    vfs_add_child(dev_root, dentry);

    return node;
}

// Walks dev_root's dentry children by name -- mirrors ramfs_finddir()'s
// pattern -- so vfs_lookup() can actually resolve paths like
// "/dev/console" down through devfs's own root inode (previously this
// inode had no finddir set at all, so any lookup past "dev" itself failed).
static inode_t* devfs_finddir(inode_t *node, char *name) {
    if (!(node->flags & VFS_DIRECTORY)) {
        return NULL;
    }
    dentry_t *dir_dentry = (dentry_t*)node->ptr;
    dentry_t *child = dir_dentry->first_child;
    while (child != NULL) {
        if (strcmp(child->name, name) == 0) {
            return child->inode;
        }
        child = child->next_sibling;
    }
    return NULL;
}

dentry_t* devfs_init() {
    klog(LOG_INFO, "devfs", "Initializing...");

    // Create a root inode for /dev
    inode_t *dev_root_inode = (inode_t*)k_malloc(sizeof(inode_t));
    dentry_t *dev_root_dentry = (dentry_t*)k_malloc(sizeof(dentry_t));
    if (dev_root_inode == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate memory for /dev inode!");
        return NULL;
    }
    memset(dev_root_inode, 0, sizeof(inode_t));
    strcpy(dev_root_inode->name, "dev");
    dev_root_inode->flags = VFS_DIRECTORY;
    dev_root_inode->ptr = (void*)dev_root_dentry;
    dev_root_inode->stat = devfs_stat;
    dev_root_inode->finddir = devfs_finddir;

    // Create a dentry for /dev
    if (dev_root_dentry == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate memory for /dev dentry!");
        k_free(dev_root_inode);
        return NULL;
    }
    memset(dev_root_dentry, 0, sizeof(dentry_t));
    strcpy(dev_root_dentry->name, "dev");
    dev_root_dentry->inode = dev_root_inode;
    dev_root_dentry->parent = NULL;
    dev_root_dentry->first_child = NULL;
    dev_root_dentry->next_sibling = NULL;

    devfs_add_device(dev_root_dentry, "console", tty_dev_read, tty_dev_write, tty_dev_ioctl, tty_console);
    devfs_add_device(dev_root_dentry, "tty", tty_dev_read, tty_dev_write, tty_dev_ioctl, NULL);
    devfs_add_device(dev_root_dentry, "tty1", tty_dev_read, tty_dev_write, tty_dev_ioctl, &tty_vga);
    devfs_add_device(dev_root_dentry, "ttyS0", tty_dev_read, tty_dev_write, tty_dev_ioctl, &tty_serial);
    devfs_add_device(dev_root_dentry, "null", null_read, null_write, NULL, NULL);
    devfs_add_device(dev_root_dentry, "zero", zero_read, null_write, NULL, NULL);

    klog(LOG_OK, "devfs", "Initialized.");
    return dev_root_dentry;
}
