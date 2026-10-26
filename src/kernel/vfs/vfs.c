#include <kernel/vfs/vfs.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/video/vga.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/hal/clock.h>
#include <kernel/errno.h>
#include <kernel/proc/cred.h>

#define MAX_SYMLINKS 40 /* ELOOP after this many, as on Linux */

dentry_t *fs_root;

dentry_t* vfs_init() {
    inode_t *dev;

    fs_root = ramfs_init();

    // devfs is mounted on /dev
    dentry_t *dev_dentry = devfs_init();
    if (dev_dentry != NULL && vfs_create(fs_root->inode, "dev", VFS_DIRECTORY, 0755, &dev) == 0) {
        vfs_mount((dentry_t*)dev->ptr, dev_dentry);
        terminal_print_colorful("VFS: Mounted /dev filesystem.\n", VGA_COLOR_GREEN);
    } else {
        terminal_print_colorful("VFS: Failed to mount /dev.\n", VGA_COLOR_RED);
    }

    terminal_print_colorful("VFS: Initialized. Current VFS tree:\n", VGA_COLOR_CYAN);
    vfs_debug_print_tree(fs_root, 0);

    return fs_root;
}

// Fresh metadata owned by root; vfs_create() gives it to the creator.
void vfs_init_inode(inode_t *n, uint32_t type, uint32_t mode) {
    n->flags = type;
    n->mode = mode & 07777;
    n->uid = n->gid = 0;
    n->nlink = 1;
    n->atime = n->mtime = n->ctime = clock_realtime_ns();
}

void vfs_touch(inode_t *n, int which) {
    uint64_t now = clock_realtime_ns();

    if (which & VFS_ATIME) {
        n->atime = now;
    }
    if (which & VFS_MTIME) {
        n->mtime = now;
    }
    if (which & VFS_CTIME) {
        n->ctime = now;
    }
}

static bool is_dir(inode_t *n) {
    return (n->flags & VFS_TYPE) == VFS_DIRECTORY;
}

dentry_t *vfs_child(inode_t *dir, const char *name) {
    if (!is_dir(dir) || dir->ptr == NULL) {
        return NULL;
    }
    for (dentry_t *c = ((dentry_t *)dir->ptr)->first_child; c != NULL; c = c->next_sibling) {
        if (strcmp(c->name, name) == 0) {
            return c;
        }
    }
    return NULL;
}

inode_t *vfs_finddir_dentry(inode_t *dir, char *name) {
    dentry_t *c = vfs_child(dir, name);

    return c != NULL ? c->inode : NULL;
}

// One path component. Directory inodes keep their dentry in ->ptr, which
// is how ".." finds the parent; "/.." is "/".
static inode_t *step(inode_t *dir, const char *name) {
    if (strcmp(name, ".") == 0) {
        return dir;
    }
    if (strcmp(name, "..") == 0) {
        dentry_t *d = (dentry_t *)dir->ptr;
        return d != NULL && d->parent != NULL ? d->parent->inode : dir;
    }
    return dir->finddir != NULL ? dir->finddir(dir, (char *)name) : NULL;
}

static inode_t *walk(inode_t *base, const char *path, bool follow, int depth, long *err) {
    char comp[NAME_MAX + 1];
    inode_t *cur = path[0] == '/' ? fs_root->inode : base;

    while (*path != '\0') {
        size_t len = 0;

        while (*path == '/') {
            path++;
        }
        if (*path == '\0') {
            break;
        }
        while (path[len] != '\0' && path[len] != '/') {
            len++;
        }
        if (len > NAME_MAX) {
            *err = -ENAMETOOLONG;
            return NULL;
        }
        memcpy(comp, path, len);
        comp[len] = '\0';
        path += len;

        if (!is_dir(cur)) {
            *err = -ENOTDIR;
            return NULL;
        }
        if ((*err = cred_permission(cur, MAY_EXEC)) < 0) {
            return NULL;
        }
        inode_t *next = step(cur, comp);
        if (next == NULL) {
            *err = -ENOENT;
            return NULL;
        }

        /* A trailing slash makes the last component a directory, so a
         * symlink there is always followed, as on Linux. */
        bool last = *path == '\0';
        if ((next->flags & VFS_TYPE) == VFS_SYMLINK && (!last || follow)) {
            char *target;

            if (depth == MAX_SYMLINKS) {
                *err = -ELOOP;
                return NULL;
            }
            target = k_malloc(next->length + 1);
            if (target == NULL) {
                *err = -ENOMEM;
                return NULL;
            }
            next->read(next, 0, next->length, (uint8_t *)target);
            target[next->length] = '\0';
            next = walk(cur, target, true, depth + 1, err); /* relative to the link's dir */
            k_free(target);
            if (next == NULL) {
                return NULL;
            }
        }
        cur = next;
        if (*path == '/' && !is_dir(cur)) {
            bool trailing = true;
            for (const char *p = path; *p != '\0'; p++) {
                trailing &= *p == '/';
            }
            if (trailing) {
                *err = -ENOTDIR; /* "file/" */
                return NULL;
            }
        }
    }
    return cur;
}

inode_t *vfs_resolve(inode_t *base, const char *path, bool follow, long *err) {
    if (path[0] == '\0') {
        *err = -ENOENT;
        return NULL;
    }
    return walk(base, path, follow, 0, err);
}

inode_t* vfs_lookup(inode_t *base, char *path) {
    long err;

    return vfs_resolve(base, path, true, &err);
}

inode_t *vfs_parent(inode_t *base, const char *path, char name[NAME_MAX + 1], long *err) {
    size_t end = strlen(path), start;
    char *dir;
    inode_t *n;

    while (end > 1 && path[end - 1] == '/') {
        end--; /* "a/b/" names b */
    }
    start = end;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    if (end - start > NAME_MAX) {
        *err = -ENAMETOOLONG;
        return NULL;
    }
    memcpy(name, path + start, end - start);
    name[end - start] = '\0';
    if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        *err = name[0] == '\0' && end > 0 ? -EEXIST : -EINVAL; /* "/" or "." */
        return NULL;
    }
    if (start == 0) {
        return base;
    }
    dir = k_malloc(start + 1);
    if (dir == NULL) {
        *err = -ENOMEM;
        return NULL;
    }
    memcpy(dir, path, start);
    dir[start] = '\0';
    n = vfs_resolve(base, dir, true, err);
    k_free(dir);
    if (n != NULL && !is_dir(n)) {
        *err = -ENOTDIR;
        return NULL;
    }
    return n;
}

long vfs_create(inode_t *dir, const char *name, uint32_t type, uint32_t mode, inode_t **out) {
    inode_t *n;
    long err;

    if (!is_dir(dir)) {
        return -ENOTDIR;
    }
    if (strlen(name) > NAME_MAX) {
        return -ENAMETOOLONG;
    }
    if (vfs_child(dir, name) != NULL) {
        return -EEXIST;
    }
    if (dir->create == NULL) {
        return -EPERM;
    }
    if ((err = cred_permission(dir, MAY_WRITE | MAY_EXEC)) < 0) {
        return err;
    }
    n = dir->create(dir, name, type, mode);
    if (n == NULL) {
        return -ENOMEM;
    }
    /* The creator owns it; a setgid directory passes its group on. */
    n->uid = cred_euid();
    if (dir->mode & S_ISGID) {
        n->gid = dir->gid;
        if (type == VFS_DIRECTORY) {
            n->mode |= S_ISGID;
        }
    } else {
        n->gid = cred_egid();
    }
    vfs_touch(dir, VFS_MTIME | VFS_CTIME);
    if (out != NULL) {
        *out = n;
    }
    return 0;
}

static void detach(dentry_t *d) {
    dentry_t **pp = &d->parent->first_child;

    while (*pp != d) {
        pp = &(*pp)->next_sibling;
    }
    *pp = d->next_sibling;
    d->next_sibling = NULL;
}

/* Drop a name. The inode goes when no name and no open file is left. */
static void drop(inode_t *dir, dentry_t *d) {
    inode_t *n = d->inode;

    detach(d);
    k_free(d);
    if (is_dir(n)) {
        n->ptr = NULL;
    }
    n->nlink--;
    vfs_touch(n, VFS_CTIME);
    vfs_touch(dir, VFS_MTIME | VFS_CTIME);
    if (n->nlink == 0 && n->refs == 0 && n->destroy != NULL) {
        n->destroy(n);
    }
}

/* Write and search on the directory, and the sticky rule. */
static long may_delete(inode_t *dir, inode_t *victim) {
    long err = cred_permission(dir, MAY_WRITE | MAY_EXEC);

    if (err < 0) {
        return err;
    }
    return cred_may_delete(dir, victim) ? 0 : -EPERM;
}

long vfs_unlink(inode_t *dir, const char *name) {
    dentry_t *d = vfs_child(dir, name);
    long err;

    if (d == NULL) {
        return is_dir(dir) ? -ENOENT : -ENOTDIR;
    }
    if (is_dir(d->inode)) {
        return -EISDIR;
    }
    if (dir->create == NULL) {
        return -EPERM; /* devfs nodes stay */
    }
    if ((err = may_delete(dir, d->inode)) < 0) {
        return err;
    }
    drop(dir, d);
    return 0;
}

static bool mountpoint(inode_t *n) {
    return n->dev != ((dentry_t *)n->ptr)->parent->inode->dev;
}

long vfs_rmdir(inode_t *dir, const char *name) {
    dentry_t *d = vfs_child(dir, name);
    long err;

    if (d == NULL) {
        return is_dir(dir) ? -ENOENT : -ENOTDIR;
    }
    if (!is_dir(d->inode)) {
        return -ENOTDIR;
    }
    if (mountpoint(d->inode)) {
        return -EBUSY;
    }
    if (d->first_child != NULL) {
        return -ENOTEMPTY;
    }
    if ((err = may_delete(dir, d->inode)) < 0) {
        return err;
    }
    drop(dir, d);
    return 0;
}

long vfs_link(inode_t *dir, const char *name, inode_t *target) {
    dentry_t *d;
    long err;

    if (is_dir(target)) {
        return -EPERM;
    }
    if (!is_dir(dir)) {
        return -ENOTDIR;
    }
    if (dir->dev != target->dev || dir->create == NULL) {
        return -EXDEV;
    }
    if (vfs_child(dir, name) != NULL) {
        return -EEXIST;
    }
    if (strlen(name) > NAME_MAX) {
        return -ENAMETOOLONG;
    }
    if ((err = cred_permission(dir, MAY_WRITE | MAY_EXEC)) < 0) {
        return err;
    }
    d = k_malloc(sizeof(*d));
    if (d == NULL) {
        return -ENOMEM;
    }
    memset(d, 0, sizeof(*d));
    strcpy(d->name, name);
    d->inode = target;
    vfs_add_child((dentry_t *)dir->ptr, d);
    target->nlink++;
    vfs_touch(target, VFS_CTIME);
    vfs_touch(dir, VFS_MTIME | VFS_CTIME);
    return 0;
}

long vfs_rename(inode_t *odir, const char *oname, inode_t *ndir, const char *nname,
                bool noreplace) {
    dentry_t *src = vfs_child(odir, oname), *dst;
    long err;

    if (src == NULL) {
        return -ENOENT;
    }
    if (!is_dir(ndir)) {
        return -ENOTDIR;
    }
    if (odir->dev != ndir->dev || ndir->create == NULL) {
        return -EXDEV;
    }
    if (strlen(nname) > NAME_MAX) {
        return -ENAMETOOLONG;
    }
    if (is_dir(src->inode)) {
        if (mountpoint(src->inode)) {
            return -EBUSY;
        }
        for (dentry_t *a = (dentry_t *)ndir->ptr; a != NULL; a = a->parent) {
            if (a == src) {
                return -EINVAL; /* into its own subtree */
            }
        }
    }

    /* Removing from odir, adding to ndir; a moved directory's ".." too. */
    if ((err = may_delete(odir, src->inode)) < 0 ||
        (err = cred_permission(ndir, MAY_WRITE | MAY_EXEC)) < 0 ||
        (is_dir(src->inode) && odir != ndir &&
         (err = cred_permission(src->inode, MAY_WRITE)) < 0)) {
        return err;
    }
    dst = vfs_child(ndir, nname);
    if (dst != NULL && !cred_may_delete(ndir, dst->inode)) {
        return -EPERM;
    }
    if (dst != NULL) {
        if (noreplace) {
            return -EEXIST;
        }
        if (dst->inode == src->inode) {
            return 0; /* two names of the same file: nothing to do */
        }
        if (is_dir(src->inode) && !is_dir(dst->inode)) {
            return -ENOTDIR;
        }
        if (!is_dir(src->inode) && is_dir(dst->inode)) {
            return -EISDIR;
        }
        if (is_dir(dst->inode) && (dst->first_child != NULL || mountpoint(dst->inode))) {
            return -ENOTEMPTY;
        }
        drop(ndir, dst);
    }

    detach(src);
    strcpy(src->name, nname);
    vfs_add_child((dentry_t *)ndir->ptr, src);
    vfs_touch(src->inode, VFS_CTIME);
    vfs_touch(odir, VFS_MTIME | VFS_CTIME);
    vfs_touch(ndir, VFS_MTIME | VFS_CTIME);
    return 0;
}

void vfs_add_child(dentry_t *parent, dentry_t *child) {
    child->parent = parent;
    if (parent->first_child == NULL) {
        parent->first_child = child;
    } else {
        dentry_t *current = parent->first_child;
        while (current->next_sibling != NULL) {
            current = current->next_sibling;
        }
        current->next_sibling = child;
    }
}
void vfs_mount(dentry_t *mountpoint, dentry_t *root) {
    mountpoint->inode = root->inode;
    mountpoint->first_child = root->first_child;
    for (dentry_t *c = mountpoint->first_child; c != NULL; c = c->next_sibling) {
        c->parent = mountpoint;
    }
    root->inode->ptr = mountpoint;
    k_free(root);
}
void vfs_debug_print_tree(dentry_t *dentry, int level) {
    for (int i = 0; i < level; i++) {
        terminal_print("|   ");
    }
    terminal_print("|-- ");
    terminal_print(dentry->name);
    terminal_print("\n");

    dentry_t *child = dentry->first_child;
    while (child != NULL) {
        vfs_debug_print_tree(child, level + 1);
        child = child->next_sibling;
    }
}
