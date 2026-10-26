#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/hal/io.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/drivers/tty.h>
#include <kernel/drivers/random.h>
#include <kernel/proc/scheduler.h>
#include <kernel/mm/vmm.h>
#include <kernel/errno.h>


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

static int tty_dev_poll(inode_t *node, uint32_t flags) {
    (void)flags;
    return POLLOUT | (tty_readable(dev_tty(node)) ? POLLIN : 0);
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

// /dev/random, /dev/urandom: the kernel generator; writes are mixed in.
static long random_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    random_bytes(buffer, size);
    return size;
}

static long random_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i + 8 <= size; i += 8) {
        uint64_t v;
        memcpy(&v, buffer + i, 8);
        random_mix(v);
    }
    return size;
}

// /dev/zero: endless zero bytes.
static long zero_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

#define DEVFS_DEV 5
#define GID_TTY 5
#define GID_DIALOUT 20
#define MKDEV(ma, mi) ((uint32_t)(ma) << 8 | (mi))

static uint32_t next_ino = 1;

/* Device nodes with Linux's numbers, modes and groups. */
static const struct {
    const char *name;
    read_type_t read;
    write_type_t write;
    ioctl_type_t ioctl;
    tty_t *tty;
    uint32_t rdev, mode, gid;
} nodes[] = {
    { "console", tty_dev_read, tty_dev_write, tty_dev_ioctl, NULL, MKDEV(5, 1), 0600, 0 },
    { "tty", tty_dev_read, tty_dev_write, tty_dev_ioctl, NULL, MKDEV(5, 0), 0666, 0 },
    { "tty1", tty_dev_read, tty_dev_write, tty_dev_ioctl, &tty_vga, MKDEV(4, 1), 0620, GID_TTY },
    { "ttyS0", tty_dev_read, tty_dev_write, tty_dev_ioctl, &tty_serial, MKDEV(4, 64), 0660, GID_DIALOUT },
    { "null", null_read, null_write, NULL, NULL, MKDEV(1, 3), 0666, 0 },
    { "zero", zero_read, null_write, NULL, NULL, MKDEV(1, 5), 0666, 0 },
    { "random", random_read, random_write, NULL, NULL, MKDEV(1, 8), 0666, 0 },
    { "urandom", random_read, random_write, NULL, NULL, MKDEV(1, 9), 0666, 0 },
};

static inode_t *devfs_inode(dentry_t *d, const char *name, uint32_t type, uint32_t mode) {
    inode_t *n = k_malloc(sizeof(*n));

    if (n == NULL) {
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    vfs_init_inode(n, type, mode);
    strcpy(n->name, name);
    strcpy(d->name, name);
    n->inode = next_ino++;
    n->dev = DEVFS_DEV;
    d->inode = n;
    return n;
}

dentry_t* devfs_init() {
    dentry_t *root = k_malloc(sizeof(*root));
    inode_t *dir;

    if (root == NULL) {
        klog(LOG_FAIL, "devfs", "Failed to allocate /dev");
        return NULL;
    }
    memset(root, 0, sizeof(*root));
    dir = devfs_inode(root, "dev", VFS_DIRECTORY, 0755);
    if (dir == NULL) {
        k_free(root);
        return NULL;
    }
    dir->ptr = root;
    dir->finddir = vfs_finddir_dentry;

    for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
        dentry_t *d = k_malloc(sizeof(*d));
        inode_t *n;

        if (d == NULL) {
            break;
        }
        memset(d, 0, sizeof(*d));
        n = devfs_inode(d, nodes[i].name, VFS_CHARDEVICE, nodes[i].mode);
        if (n == NULL) {
            k_free(d);
            break;
        }
        n->read = nodes[i].read;
        n->write = nodes[i].write;
        n->ioctl = nodes[i].ioctl;
        n->poll = nodes[i].ioctl != NULL ? tty_dev_poll : NULL;
        n->ptr = nodes[i].tty;
        n->rdev = nodes[i].rdev;
        n->gid = nodes[i].gid;
        vfs_add_child(root, d);
    }
    // /dev/console follows the console= choice
    vfs_child(dir, "console")->inode->ptr = tty_console;

    klog(LOG_OK, "devfs", "Initialized.");
    return root;
}
