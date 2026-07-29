#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/hal/io.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/drivers/keyboard.h>

static uint32_t next_devfs_inode = 1;

// /dev/console: a write-only character device that forwards bytes straight
// to the VGA text-mode terminal. Reading from it isn't supported yet (use
// /dev/keyboard for input).
static uint32_t console_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static uint32_t console_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        terminal_putchar((char)buffer[i]);
    }
    return size;
}

// /dev/keyboard: a read-only character device draining the keyboard
// driver's internal ring buffer (keyboard_get_char()). Non-blocking: a read
// returns as many bytes as are currently available (possibly 0) rather
// than waiting for more input.
static uint32_t keyboard_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    uint32_t read_count = 0;
    char c;
    while (read_count < size && keyboard_get_char(&c)) {
        buffer[read_count++] = (uint8_t)c;
    }
    return read_count;
}

static uint32_t keyboard_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    // Input-only device: writes are silently discarded, matching typical
    // Unix behavior for read-only device nodes.
    return 0;
}

void dev_open(inode_t *node) {
    klog(LOG_DEBUG, "devfs", "Opening device %s", node->name);
}

void dev_close(inode_t *node) {
    klog(LOG_DEBUG, "devfs", "Closing device %s", node->name);
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
static inode_t* devfs_add_device(dentry_t *dev_root, const char *name, read_type_t read_fn, write_type_t write_fn) {
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
    node->open = dev_open;
    node->close = dev_close;
    node->stat = devfs_stat;

    memset(dentry, 0, sizeof(dentry_t));
    strcpy(dentry->name, name);
    dentry->inode = node;
    vfs_mount(dev_root, dentry);

    klog(LOG_DEBUG, "devfs", "Created device node '%s'", name);
    return node;
}

// Walks dev_root's dentry children by name -- mirrors ramfs_finddir()'s
// pattern -- so vfs_lookup() can actually resolve paths like
// "/dev/dev/console" down through devfs's own root inode (previously this
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

    devfs_add_device(dev_root_dentry, "console", console_read, console_write);
    devfs_add_device(dev_root_dentry, "keyboard", keyboard_read, keyboard_write);

    klog(LOG_OK, "devfs", "Initialized.");
    return dev_root_dentry;
}
