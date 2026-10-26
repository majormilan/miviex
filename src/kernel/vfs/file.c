#include <kernel/vfs/vfs.h>
#include <kernel/fcntl.h>
#include <kernel/errno.h>
#include <kernel/mm/memory.h>
#include <kernel/libc/string.h>

static bool is_dir(inode_t *n) {
    return (n->flags & VFS_TYPE) == VFS_DIRECTORY;
}

/* Find path under base; with O_CREAT the last component may be new. */
static inode_t *resolve(inode_t *base, const char *path, uint32_t flags, uint32_t mode, long *err) {
    char name[NAME_MAX + 1];
    inode_t *dir, *node;

    node = vfs_resolve(base, path, !(flags & O_NOFOLLOW), err);
    if (node != NULL) {
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            *err = -EEXIST;
            return NULL;
        }
        if ((node->flags & VFS_TYPE) == VFS_SYMLINK) {
            *err = -ELOOP; /* O_NOFOLLOW on a symlink */
            return NULL;
        }
        return node;
    }
    if (!(flags & O_CREAT) || *err != -ENOENT) {
        return NULL;
    }
    dir = vfs_parent(base, path, name, err);
    if (dir == NULL) {
        return NULL;
    }
    if ((*err = vfs_create(dir, name, VFS_FILE, mode, &node)) < 0) {
        return NULL;
    }
    return node;
}

file_t *file_open(inode_t *base, const char *path, uint32_t flags, uint32_t mode, long *err) {
    inode_t *node = resolve(base, path, flags, mode, err);
    uint32_t acc = flags & O_ACCMODE;
    file_t *f;

    if (node == NULL) {
        return NULL;
    }
    if ((flags & O_DIRECTORY) && !is_dir(node)) {
        *err = -ENOTDIR;
        return NULL;
    }
    if (is_dir(node) && acc != O_RDONLY) {
        *err = -EISDIR;
        return NULL;
    }
    if ((flags & O_TRUNC) && acc != O_RDONLY && node->truncate != NULL) {
        node->truncate(node, 0);
    }

    f = file_alloc(node, flags);
    if (f == NULL) {
        *err = -ENOMEM;
    }
    return f;
}

file_t *file_alloc(inode_t *node, uint32_t flags) {
    file_t *f = k_malloc(sizeof(*f));

    if (f != NULL) {
        f->inode = node;
        f->offset = 0;
        f->flags = flags;
        f->refs = 1;
        node->refs++;
    }
    return f;
}

file_t *file_get(file_t *f) {
    f->refs++;
    return f;
}

/* The last close of a description; an unlinked inode goes with it. A
 * release hook may free the inode itself (pipes), so read it first. */
void file_put(file_t *f) {
    inode_t *n;
    bool gone;

    if (f == NULL || --f->refs != 0) {
        return;
    }
    n = f->inode;
    gone = --n->refs == 0 && n->nlink == 0 && n->destroy != NULL;
    if (n->release != NULL) {
        n->release(n, f->flags);
    }
    if (gone) {
        n->destroy(n);
    }
    k_free(f);
}
