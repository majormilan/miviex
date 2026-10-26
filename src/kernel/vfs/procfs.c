#include <kernel/vfs/vfs.h>
#include <kernel/vfs/procfs.h>
#include <kernel/proc/scheduler.h>
#include <kernel/mm/memory.h>
#include <kernel/libc/string.h>
#include <kernel/libc/stdlib.h>

/*
 * /proc, so far: self -> <pid>, and <pid>/fd/<n> -> the path of each open
 * file (what musl's ttyname() reads). Entries are made on lookup and
 * freed when their process is reaped, unless something still uses them.
 */

#define PROCFS_DEV 3

enum { PID_DIR, FD_DIR, FD_LINK };

typedef struct pent {
    uint32_t pid;
    int fd, kind;
    struct pent *next;
    inode_t n;
    dentry_t d;
} pent_t;

static pent_t *ents;
static inode_t root, self;
static uint32_t next_ino = 2;

static process_t *live(uint32_t pid) {
    process_t *p = process_find(pid);

    return p != NULL && p->is_user && p->state != PROCESS_TERMINATED ? p : NULL;
}

static long number(const char *s) {
    long v = 0;

    if (*s == '\0') {
        return -1;
    }
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9' || v > 100000000) {
            return -1;
        }
        v = v * 10 + (*s - '0');
    }
    return v;
}

static pent_t *of_dir(inode_t *n) {
    return (pent_t *)((char *)n->ptr - __builtin_offsetof(pent_t, d));
}

/* What an fd link points at: the path it was opened by. */
static const char *target(pent_t *e) {
    process_t *p = live(e->pid);
    file_t *f = p != NULL ? p->fds[e->fd] : NULL;

    if (f == NULL) {
        return "";
    }
    return f->path != NULL ? f->path : (f->inode->flags & VFS_TYPE) == VFS_PIPE ? "pipe:" : "anon_inode";
}

static long copy_out(const char *s, uint32_t off, uint32_t size, uint8_t *buf) {
    uint32_t len = (uint32_t)strlen(s);

    if (off >= len) {
        return 0;
    }
    if (size > len - off) {
        size = len - off;
    }
    memcpy(buf, s + off, size);
    return size;
}

static long fd_read(inode_t *n, uint32_t off, uint32_t size, uint8_t *buf) {
    return copy_out(target((pent_t *)n->ptr), off, size, buf);
}

static long self_read(inode_t *n, uint32_t off, uint32_t size, uint8_t *buf) {
    char s[16];

    (void)n;
    uitoa(scheduler_current()->pid, s, 10);
    return copy_out(s, off, size, buf);
}

static inode_t *pid_find(inode_t *dir, char *name);
static inode_t *fd_find(inode_t *dir, char *name);

static pent_t *get(uint32_t pid, int fd, int kind, dentry_t *parent, const char *name) {
    pent_t *e;

    for (e = ents; e != NULL; e = e->next) {
        if (e->pid == pid && e->fd == fd && e->kind == kind) {
            return e;
        }
    }
    if ((e = k_malloc(sizeof(*e))) == NULL) {
        return NULL;
    }
    memset(e, 0, sizeof(*e));
    e->pid = pid;
    e->fd = fd;
    e->kind = kind;
    vfs_init_inode(&e->n, kind == FD_LINK ? VFS_SYMLINK : VFS_DIRECTORY,
                   kind == PID_DIR ? 0555 : kind == FD_DIR ? 0500 : 0700);
    e->n.inode = next_ino++;
    e->n.dev = PROCFS_DEV;
    strcpy(e->n.name, name);
    strcpy(e->d.name, name);
    e->d.inode = &e->n;
    e->d.parent = parent; /* for "..", but not listed yet */
    if (kind == FD_LINK) {
        e->n.read = fd_read;
        e->n.ptr = e;
    } else {
        e->n.finddir = kind == PID_DIR ? pid_find : fd_find;
        e->n.ptr = &e->d;
    }
    e->next = ents;
    ents = e;
    return e;
}

/* Owned by the process, as on Linux. */
static inode_t *owned(pent_t *e, process_t *p) {
    if (e == NULL) {
        return NULL;
    }
    e->n.uid = p->euid;
    e->n.gid = p->egid;
    return &e->n;
}

static inode_t *fd_find(inode_t *dir, char *name) {
    pent_t *d = of_dir(dir);
    process_t *p = live(d->pid);
    long fd = number(name);
    pent_t *e;

    if (p == NULL || fd < 0 || fd >= MAX_FDS || p->fds[fd] == NULL) {
        return NULL;
    }
    e = get(d->pid, (int)fd, FD_LINK, &d->d, name);
    if (e != NULL) {
        e->n.length = (uint32_t)strlen(target(e));
    }
    return owned(e, p);
}

static inode_t *pid_find(inode_t *dir, char *name) {
    pent_t *d = of_dir(dir);
    process_t *p = live(d->pid);

    if (p == NULL || strcmp(name, "fd") != 0) {
        return NULL;
    }
    return owned(get(d->pid, -1, FD_DIR, &d->d, "fd"), p);
}

static inode_t *root_find(inode_t *dir, char *name) {
    long pid;
    process_t *p;

    (void)dir;
    if (strcmp(name, "self") == 0) {
        char s[16];
        uitoa(scheduler_current()->pid, s, 10);
        self.length = (uint32_t)strlen(s);
        return &self;
    }
    if ((pid = number(name)) < 0 || (p = live((uint32_t)pid)) == NULL) {
        return NULL;
    }
    return owned(get((uint32_t)pid, -1, PID_DIR, (dentry_t *)root.ptr, name), p);
}

void procfs_forget(uint32_t pid) {
    for (pent_t **pp = &ents; *pp != NULL;) {
        pent_t *e = *pp;
        if (e->pid == pid && e->n.refs == 0) {
            *pp = e->next;
            k_free(e);
        } else {
            pp = &e->next;
        }
    }
}

dentry_t *procfs_init(void) {
    dentry_t *d = k_malloc(sizeof(*d));

    if (d == NULL) {
        return NULL;
    }
    memset(d, 0, sizeof(*d));
    vfs_init_inode(&root, VFS_DIRECTORY, 0555);
    root.inode = 1;
    root.dev = PROCFS_DEV;
    root.finddir = root_find;
    root.ptr = d;
    strcpy(root.name, "proc");
    strcpy(d->name, "proc");
    d->inode = &root;

    vfs_init_inode(&self, VFS_SYMLINK, 0777);
    self.inode = next_ino++;
    self.dev = PROCFS_DEV;
    self.read = self_read;
    strcpy(self.name, "self");
    return d;
}
