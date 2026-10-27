#include <kernel/vfs/vfs.h>
#include <kernel/drivers/device.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>

/*
 * devfs: /dev, filled by drivers through dev_register(). Like devtmpfs,
 * nodes cannot be created or removed from user space.
 */

#define DEVFS_DEV 5
#define MKDEV(ma, mi) ((uint32_t)(ma) << 8 | (mi))

static inode_t *dir;
static uint32_t next_ino = 2;

inode_t *dev_register(const char *name, uint32_t major, uint32_t minor, uint32_t mode,
                      uint32_t gid, const chardev_ops_t *ops, void *ctx) {
    inode_t *n = k_malloc(sizeof(*n));
    dentry_t *d = k_malloc(sizeof(*d));

    if (dir == NULL || n == NULL || d == NULL) {
        klog(LOG_FAIL, "devfs", "Cannot register /dev/%s", name);
        k_free(n);
        k_free(d);
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    memset(d, 0, sizeof(*d));
    vfs_init_inode(n, VFS_CHARDEVICE, mode);
    strcpy(n->name, name);
    strcpy(d->name, name);
    n->inode = next_ino++;
    n->dev = DEVFS_DEV;
    n->rdev = MKDEV(major, minor);
    n->gid = gid;
    n->read = ops->read;
    n->write = ops->write;
    n->ioctl = ops->ioctl;
    n->poll = ops->poll;
    n->open = ops->open;
    n->ptr = ctx;
    d->inode = n;
    vfs_add_child((dentry_t *)dir->ptr, d); /* the mountpoint, once mounted */
    return n;
}

dentry_t *devfs_init(void) {
    dentry_t *root = k_malloc(sizeof(*root));

    if (root == NULL || (dir = k_malloc(sizeof(*dir))) == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate /dev");
        k_free(root);
        return NULL;
    }
    memset(root, 0, sizeof(*root));
    memset(dir, 0, sizeof(*dir));
    vfs_init_inode(dir, VFS_DIRECTORY, 0755);
    strcpy(dir->name, "dev");
    strcpy(root->name, "dev");
    dir->inode = 1;
    dir->dev = DEVFS_DEV;
    dir->finddir = vfs_finddir_dentry;
    dir->ptr = root;
    root->inode = dir;
    return root;
}
