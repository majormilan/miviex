#include <kernel/vfs/ramfs.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/errno.h>
#include <kernel/log.h>

static uint32_t next_ino = 1;

static long ramfs_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    ramfs_file_data_t *fd = (ramfs_file_data_t *)node->ptr;

    if ((node->flags & VFS_TYPE) == VFS_DIRECTORY) {
        return -EISDIR;
    }
    if (fd == NULL || offset >= fd->size) {
        return 0;
    }
    if (size > fd->size - offset) {
        size = fd->size - offset;
    }
    memcpy(buffer, fd->data + offset, size);
    return size;
}

/* Make room for `size` bytes; new bytes read as zero. */
static int reserve(inode_t *node, uint32_t size) {
    ramfs_file_data_t *fd = (ramfs_file_data_t *)node->ptr;
    uint8_t *data;
    uint32_t cap;

    if (fd == NULL) {
        fd = k_malloc(sizeof(*fd));
        if (fd == NULL) {
            return -ENOMEM;
        }
        memset(fd, 0, sizeof(*fd));
        node->ptr = fd;
    }
    if (size <= fd->capacity) {
        return 0;
    }
    cap = (uint32_t)align_up_page(size);
    data = k_malloc(cap);
    if (data == NULL) {
        return -ENOMEM;
    }
    memset(data, 0, cap);
    if (fd->data != NULL) {
        memcpy(data, fd->data, fd->size);
        k_free(fd->data);
    }
    fd->data = data;
    fd->capacity = cap;
    return 0;
}

static long ramfs_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    ramfs_file_data_t *fd;
    int err;

    if ((node->flags & VFS_TYPE) == VFS_DIRECTORY) {
        return -EISDIR;
    }
    if (offset + size < offset) {
        return -EINVAL;
    }
    if ((err = reserve(node, offset + size)) < 0) {
        return err;
    }
    fd = (ramfs_file_data_t *)node->ptr;
    if (offset > fd->size) {
        memset(fd->data + fd->size, 0, offset - fd->size);
    }
    memcpy(fd->data + offset, buffer, size);
    if (offset + size > fd->size) {
        fd->size = offset + size;
    }
    node->length = fd->size;
    vfs_touch(node, VFS_MTIME | VFS_CTIME);
    return size;
}

static int ramfs_truncate(inode_t *node, uint32_t length) {
    ramfs_file_data_t *fd;
    int err;

    if ((err = reserve(node, length)) < 0) {
        return err;
    }
    fd = (ramfs_file_data_t *)node->ptr;
    if (length < fd->size) {
        memset(fd->data + length, 0, fd->size - length);
    }
    fd->size = node->length = length;
    vfs_touch(node, VFS_MTIME | VFS_CTIME);
    return 0;
}

static void ramfs_destroy(inode_t *node) {
    ramfs_file_data_t *fd = (ramfs_file_data_t *)node->ptr;

    if ((node->flags & VFS_TYPE) != VFS_DIRECTORY && fd != NULL) {
        k_free(fd->data);
        k_free(fd);
    }
    k_free(node);
}

static inode_t *ramfs_inode(uint32_t type, uint32_t mode, uint32_t dev) {
    inode_t *n = k_malloc(sizeof(*n));

    if (n == NULL) {
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    vfs_init_inode(n, type, mode);
    n->inode = next_ino++;
    n->dev = dev;
    n->read = ramfs_read;
    n->destroy = ramfs_destroy;
    if (type == VFS_DIRECTORY) {
        n->finddir = vfs_finddir_dentry;
    } else {
        n->write = ramfs_write;
        n->truncate = ramfs_truncate;
    }
    return n;
}

static inode_t *ramfs_create(inode_t *dir, const char *name, uint32_t type, uint32_t mode) {
    inode_t *n = ramfs_inode(type, mode, dir->dev);
    dentry_t *d = k_malloc(sizeof(*d));

    if (n == NULL || d == NULL) {
        k_free(n);
        k_free(d);
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    strcpy(d->name, name);
    strcpy(n->name, name);
    d->inode = n;
    if (type == VFS_DIRECTORY) {
        n->ptr = d;
        n->create = ramfs_create;
    }
    vfs_add_child((dentry_t *)dir->ptr, d);
    return n;
}

dentry_t* ramfs_init(uint32_t dev, uint32_t mode) {
    inode_t *root = ramfs_inode(VFS_DIRECTORY, mode, dev);
    dentry_t *d = k_malloc(sizeof(*d));

    if (root == NULL || d == NULL) {
        klog(LOG_FAIL, "ramfs", "Failed to allocate the root directory");
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    strcpy(d->name, "/");
    strcpy(root->name, "/");
    d->inode = root;
    root->ptr = d;
    root->create = ramfs_create;

    return d;
}
