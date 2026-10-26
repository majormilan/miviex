#include <kernel/vfs/vfs.h>
#include <kernel/fcntl.h>
#include <kernel/errno.h>
#include <kernel/mm/memory.h>
#include <kernel/libc/string.h>

#define PATH_MAX_ 128

static bool is_dir(inode_t *n) {
    return (n->flags & 0x7) == VFS_DIRECTORY;
}

/* Find or create path under base; on O_CREAT the last component may be new. */
static inode_t *resolve(inode_t *base, const char *path, uint32_t flags, long *err) {
    char buf[PATH_MAX_];
    char *slash, *name;
    inode_t *dir, *node;

    if (strlen(path) >= sizeof(buf)) {
        *err = -ENAMETOOLONG;
        return NULL;
    }
    strcpy(buf, path);

    node = vfs_lookup(base, buf);
    if (node != NULL) {
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            *err = -EEXIST;
            return NULL;
        }
        return node;
    }
    if (!(flags & O_CREAT)) {
        *err = -ENOENT;
        return NULL;
    }

    strcpy(buf, path);
    slash = strrchr(buf, '/');
    if (slash == NULL) {
        dir = base;
        name = buf;
    } else {
        *slash = '\0';
        name = slash + 1;
        dir = vfs_lookup(base, buf[0] ? buf : "/");
    }
    if (dir == NULL || !is_dir(dir)) {
        *err = dir == NULL ? -ENOENT : -ENOTDIR;
        return NULL;
    }
    if (*name == '\0') {
        *err = -EISDIR;
        return NULL;
    }
    node = vfs_create(dir, name, VFS_FILE);
    if (node == NULL) {
        *err = -EIO;
    }
    return node;
}

file_t *file_open(inode_t *base, const char *path, uint32_t flags, long *err) {
    inode_t *node = resolve(base, path, flags, err);
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
    }
    return f;
}

file_t *file_get(file_t *f) {
    f->refs++;
    return f;
}

void file_put(file_t *f) {
    if (f != NULL && --f->refs == 0) {
        if (f->inode->release != NULL) {
            f->inode->release(f->inode, f->flags);
        }
        k_free(f);
    }
}
