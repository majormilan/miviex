#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/hal/io.h>
#include <kernel/vfs/ramfs.h>

// Placeholder device read/write functions
uint32_t dev_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    klog(LOG_DEBUG, "devfs", "Reading from device %s", node->name);
    // For now, just return 0 bytes read
    return 0;
}

uint32_t dev_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    klog(LOG_DEBUG, "devfs", "Writing to device %s", node->name);
    // For now, just return 0 bytes written
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

    klog(LOG_OK, "devfs", "Initialized.");
    return dev_root_dentry;
}
