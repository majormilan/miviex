#include <kernel/vfs/vfs.h>
#include <kernel/vfs/procfs.h>
#include <kernel/proc/scheduler.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/hal/clock.h>
#include <kernel/hal/multiboot.h>
#include <kernel/drivers/tty.h>
#include <kernel/libc/string.h>
#include <kernel/libc/stdio.h>
#include <kernel/libc/stdlib.h>
#include <kernel/errno.h>
#include <kernel/version.h>

/*
 * /proc in Linux's formats, for ps, pidof, free, uptime, mount and df:
 * the system files below, self, and per process stat, status, cmdline,
 * comm, cwd, exe and fd/. Process entries are made on lookup and freed
 * when the process is reaped, unless something still uses them.
 */

#define PROCFS_DEV 3
#define TEXT_MAX 4096

enum { PID_DIR, FD_DIR, FD_LINK, PID_FILE, PID_LINK };
enum { F_CMDLINE, F_COMM, F_STAT, F_STATUS };
enum { L_CWD, L_EXE };
enum { S_MOUNTS, S_MEMINFO, S_UPTIME, S_LOADAVG, S_VERSION, S_CMDLINE, S_FILESYSTEMS, NSYS };

static const char *const sys_names[NSYS] = {
    "mounts", "meminfo", "uptime", "loadavg", "version", "cmdline", "filesystems",
};

static const struct {
    const char *name;
    int kind, which;
} pid_entries[] = {
    { "cmdline", PID_FILE, F_CMDLINE }, { "comm", PID_FILE, F_COMM },
    { "cwd", PID_LINK, L_CWD }, { "exe", PID_LINK, L_EXE }, { "fd", FD_DIR, 0 },
    { "stat", PID_FILE, F_STAT }, { "status", PID_FILE, F_STATUS },
};

#define NPID (sizeof(pid_entries) / sizeof(pid_entries[0]))

typedef struct pent {
    uint32_t pid;
    int which, kind;
    struct pent *next;
    inode_t n;
    dentry_t d;
} pent_t;

static pent_t *ents;
static inode_t root, self, sys[NSYS];
static bool mounted;

/* Stable numbers: lookups and listings agree. */
static uint32_t ino(uint32_t pid, int kind, int which) {
    return pid * 1024 + (uint32_t)kind * 128 + (uint32_t)which + 64;
}

static process_t *live(uint32_t pid) {
    process_t *p = process_find(pid);

    return p != NULL && p->is_user && p->state != PROCESS_TERMINATED ? p : NULL;
}

/* Zombies stay visible to ps until reaped. */
static process_t *visible(uint32_t pid) {
    process_t *p = process_find(pid);

    return p != NULL && (p->is_user || p->state == PROCESS_TERMINATED) && p->pid != 0 ? p : NULL;
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

static bool may_inspect(process_t *p) {
    process_t *me = scheduler_current();

    return me->euid == 0 || (me->uid == p->uid && me->uid == p->euid);
}

static long copy_out(const char *s, size_t len, uint32_t off, uint32_t size, uint8_t *buf) {
    if (off >= len) {
        return 0;
    }
    if (size > len - off) {
        size = (uint32_t)(len - off);
    }
    memcpy(buf, s + off, size);
    return size;
}

/* --- text generators: length of the text written to t --- */

static size_t appendf(char *t, size_t at, const char *fmt, ...) PRINTF_LIKE(3, 4);
static size_t appendf(char *t, size_t at, const char *fmt, ...) {
    va_list ap;
    int n;

    if (at >= TEXT_MAX) {
        return at;
    }
    va_start(ap, fmt);
    n = kvsnprintf(t + at, TEXT_MAX - at, fmt, ap);
    va_end(ap);
    at += (size_t)n;
    return at < TEXT_MAX ? at : TEXT_MAX - 1;
}

static char state_of(process_t *p) {
    switch (p->state) {
    case PROCESS_BLOCKED:    return 'S';
    case PROCESS_TERMINATED: return 'Z';
    default:                 return 'R';
    }
}

static uint64_t user_pages(process_t *p) {
    return p->as != NULL ? vmm_user_pages(p->as) : 0;
}

static uint64_t sig_mask(process_t *p, bool ignored) {
    uint64_t m = 0;

    for (int i = 0; i < NSIG; i++) {
        uint64_t h = p->sigact[i].handler;
        if (ignored ? h == SIG_IGN : h > SIG_IGN) {
            m |= 1ULL << i;
        }
    }
    return m;
}

static size_t gen_stat(process_t *p, char *t) {
    uint64_t pages = user_pages(p);
    tty_t *tty = p->ctty;

    return appendf(t, 0,
        "%u (%s) %c %u %u %u %u %d 0 0 0 0 0 %lu 0 %lu 0 20 0 1 0 %lu %lu %lu "
        "18446744073709551615 0 0 0 0 0 %lu %lu %lu %lu 0 0 0 17 0 0 0 0 0 0 "
        "0 0 %lu %lu %lu 0 0 %d\n",
        p->pid, p->comm, state_of(p), p->ppid, p->pgid, p->sid,
        tty != NULL ? tty->rdev : 0, tty != NULL ? (int)tty->pgrp : -1,
        p->cpu_ticks, p->child_ticks, p->start_ticks, (uint64_t)(pages * PAGE_SIZE), pages,
        p->sig_pending, p->sig_blocked, sig_mask(p, true), sig_mask(p, false),
        p->as != NULL ? p->as->brk_start : 0, p->arg_start, p->arg_end, p->exit_status);
}

static size_t gen_status(process_t *p, char *t) {
    static const char *const names[] = { "running", "running", "sleeping", "zombie" };
    uint64_t kb = user_pages(p) * 4;
    size_t at;

    at = appendf(t, 0, "Name:\t%s\nUmask:\t%04o\nState:\t%c (%s)\nTgid:\t%u\nPid:\t%u\n"
                       "PPid:\t%u\nTracerPid:\t0\nUid:\t%u\t%u\t%u\t%u\nGid:\t%u\t%u\t%u\t%u\n"
                       "FDSize:\t%d\nGroups:\t",
                 p->comm, p->umask, state_of(p), names[p->state], p->pid, p->pid, p->ppid,
                 p->uid, p->euid, p->suid, p->euid, p->gid, p->egid, p->sgid, p->egid, MAX_FDS);
    for (uint32_t i = 0; i < p->ngroups; i++) {
        at = appendf(t, at, "%u ", p->groups[i]);
    }
    return appendf(t, at, "\nVmSize:\t%8lu kB\nVmRSS:\t%8lu kB\nThreads:\t1\n"
                          "SigPnd:\t%016lx\nSigBlk:\t%016lx\nSigIgn:\t%016lx\nSigCgt:\t%016lx\n",
                   kb, kb, p->sig_pending, p->sig_blocked, sig_mask(p, true), sig_mask(p, false));
}

/* argv as the process sees it now, NUL-separated. */
static size_t gen_cmdline(process_t *p, char *t) {
    uint64_t len = p->arg_end - p->arg_start;

    if (p->as == NULL || p->arg_end <= p->arg_start) {
        return 0;
    }
    if (len > TEXT_MAX) {
        len = TEXT_MAX;
    }
    return vmm_copy_from(p->as, p->arg_start, t, len) < 0 ? 0 : len;
}

static size_t gen_mounts(char *t) {
    mount_t *list[32];
    size_t n = 0, at = 0;
    char path[256];

    for (mount_t *m = vfs_mounts(); m != NULL && n < 32; m = m->next) {
        list[n++] = m; /* newest first */
    }
    while (n-- > 0) {
        mount_t *m = list[n];
        if (m->covered == NULL) {
            strcpy(path, "/");
        } else if (vfs_dentry_path(m->mp, path, sizeof(path)) < 0) {
            continue;
        }
        at = appendf(t, at, "%s %s %s %s 0 0\n", m->source, path, m->type,
                     m->flags & MS_RDONLY ? "ro" : "rw");
    }
    return at;
}

static size_t gen_sys(int which, char *t) {
    pmm_stats_t st;
    uint64_t up = clock_monotonic_ns() / 10000000, idle;
    process_t *idlep = process_find(0);
    uint32_t running = 0, total = 0, last = 0;

    switch (which) {
    case S_MOUNTS:
        return gen_mounts(t);
    case S_MEMINFO:
        pmm_get_stats(&st);
        return appendf(t, 0, "MemTotal:       %8lu kB\nMemFree:        %8lu kB\n"
                             "MemAvailable:   %8lu kB\nBuffers:               0 kB\n"
                             "Cached:                0 kB\nSwapCached:            0 kB\n"
                             "SwapTotal:             0 kB\nSwapFree:              0 kB\n",
                       st.total_frames * 4, st.free_frames * 4, st.free_frames * 4);
    case S_UPTIME:
        idle = idlep != NULL ? idlep->cpu_ticks : 0;
        return appendf(t, 0, "%lu.%02lu %lu.%02lu\n", up / 100, up % 100, idle / 100, idle % 100);
    case S_LOADAVG:
        for (process_t *p = process_list(); p != NULL; p = p->all_next) {
            if (p->is_user) {
                total++;
                running += p->state == PROCESS_RUNNING || p->state == PROCESS_READY;
                last = p->pid > last ? p->pid : last;
            }
        }
        return appendf(t, 0, "%lu.%02lu %lu.%02lu %lu.%02lu %u/%u %u\n",
                       loadavg[0] >> 11, (loadavg[0] & 2047) * 100 >> 11,
                       loadavg[1] >> 11, (loadavg[1] & 2047) * 100 >> 11,
                       loadavg[2] >> 11, (loadavg[2] & 2047) * 100 >> 11, running, total, last);
    case S_VERSION:
        return appendf(t, 0, "%s version %s %s\n", UTS_SYSNAME, UTS_RELEASE, UTS_VERSION);
    case S_CMDLINE:
        return appendf(t, 0, "%s\n", get_multiboot_cmdline());
    case S_FILESYSTEMS:
        return appendf(t, 0, "nodev\trootfs\nnodev\tramfs\nnodev\ttmpfs\nnodev\tdevtmpfs\n"
                             "nodev\tproc\n");
    }
    return 0;
}

static long gen_read(size_t (*gen)(char *, void *), void *arg, uint32_t off, uint32_t size,
                     uint8_t *buf) {
    char *t = k_malloc(TEXT_MAX);
    long r;

    if (t == NULL) {
        return -ENOMEM;
    }
    r = copy_out(t, gen(t, arg), off, size, buf);
    k_free(t);
    return r;
}

static size_t gen_sys_cb(char *t, void *arg) {
    return gen_sys((int)(uintptr_t)arg, t);
}

static size_t gen_pid_cb(char *t, void *arg) {
    pent_t *e = arg;
    process_t *p = visible(e->pid);

    if (p == NULL) {
        return 0;
    }
    switch (e->which) {
    case F_CMDLINE: return gen_cmdline(p, t);
    case F_COMM:    return appendf(t, 0, "%s\n", p->comm);
    case F_STAT:    return gen_stat(p, t);
    case F_STATUS:  return gen_status(p, t);
    }
    return 0;
}

static long sys_read(inode_t *n, uint32_t off, uint32_t size, uint8_t *buf) {
    return gen_read(gen_sys_cb, (void *)(uintptr_t)(n - sys), off, size, buf);
}

static long pid_read(inode_t *n, uint32_t off, uint32_t size, uint8_t *buf) {
    return gen_read(gen_pid_cb, n->ptr, off, size, buf);
}

/* --- links --- */

static const char *fd_target(process_t *p, int fd) {
    file_t *f = p != NULL ? p->fds[fd] : NULL;

    if (f == NULL) {
        return "";
    }
    return f->path != NULL ? f->path : (f->inode->flags & VFS_TYPE) == VFS_PIPE ? "pipe:" : "anon_inode";
}

/* cwd and exe: the owner and root only, as on Linux. */
static long link_target(pent_t *e, char *t) {
    process_t *p = live(e->pid);

    if (p == NULL) {
        return -ENOENT;
    }
    if (!may_inspect(p)) {
        return -EACCES;
    }
    if (e->kind == FD_LINK) {
        strcpy(t, fd_target(p, e->which));
    } else if (e->which == L_EXE) {
        strcpy(t, p->exe != NULL ? p->exe : "");
    } else if (p->cwd == NULL || vfs_dentry_path((dentry_t *)p->cwd->ptr, t, 256) < 0) {
        strcpy(t, "/");
    }
    return (long)strlen(t);
}

static long link_read(inode_t *n, uint32_t off, uint32_t size, uint8_t *buf) {
    char t[256];
    long len = link_target((pent_t *)n->ptr, t);

    return len < 0 ? len : copy_out(t, (size_t)len, off, size, buf);
}

static long self_read(inode_t *n, uint32_t off, uint32_t size, uint8_t *buf) {
    char s[16];

    (void)n;
    uitoa(scheduler_current()->pid, s, 10);
    return copy_out(s, strlen(s), off, size, buf);
}

/* --- directories --- */

static inode_t *pid_find(inode_t *dir, char *name);
static inode_t *fd_find(inode_t *dir, char *name);
static int pid_readdir(inode_t *dir, uint64_t i, char *name, uint32_t *ino_out, uint32_t *type);
static int fd_readdir(inode_t *dir, uint64_t i, char *name, uint32_t *ino_out, uint32_t *type);

static pent_t *get(uint32_t pid, int kind, int which, dentry_t *parent, const char *name) {
    pent_t *e;
    static const uint32_t modes[] = { 0555, 0500, 0700, 0444, 0777 };

    for (e = ents; e != NULL; e = e->next) {
        if (e->pid == pid && e->which == which && e->kind == kind) {
            return e;
        }
    }
    if ((e = k_malloc(sizeof(*e))) == NULL) {
        return NULL;
    }
    memset(e, 0, sizeof(*e));
    e->pid = pid;
    e->which = which;
    e->kind = kind;
    vfs_init_inode(&e->n, kind == FD_LINK || kind == PID_LINK ? VFS_SYMLINK :
                          kind == PID_FILE ? VFS_FILE : VFS_DIRECTORY, modes[kind]);
    e->n.inode = ino(pid, kind, which);
    e->n.dev = PROCFS_DEV;
    strcpy(e->n.name, name);
    strcpy(e->d.name, name);
    e->d.inode = &e->n;
    e->d.parent = parent; /* for "..", not in the parent's list */
    switch (kind) {
    case PID_DIR:
        e->n.finddir = pid_find;
        e->n.readdir = pid_readdir;
        e->n.ptr = &e->d;
        break;
    case FD_DIR:
        e->n.finddir = fd_find;
        e->n.readdir = fd_readdir;
        e->n.ptr = &e->d;
        break;
    case PID_FILE:
        e->n.read = pid_read;
        e->n.ptr = e;
        break;
    default:
        e->n.read = link_read;
        e->n.ptr = e;
    }
    e->next = ents;
    ents = e;
    return e;
}

/* Owned by the process, as on Linux; links know their target length. */
static inode_t *owned(pent_t *e, process_t *p) {
    char t[256];
    long len;

    if (e == NULL) {
        return NULL;
    }
    e->n.uid = p->euid;
    e->n.gid = p->egid;
    if (e->kind == FD_LINK || e->kind == PID_LINK) {
        len = link_target(e, t);
        e->n.length = len > 0 ? (uint32_t)len : 0;
    }
    return &e->n;
}

static inode_t *fd_find(inode_t *dir, char *name) {
    pent_t *d = of_dir(dir);
    process_t *p = live(d->pid);
    long fd = number(name);

    if (p == NULL || fd < 0 || fd >= MAX_FDS || p->fds[fd] == NULL) {
        return NULL;
    }
    return owned(get(d->pid, FD_LINK, (int)fd, &d->d, name), p);
}

static int fd_readdir(inode_t *dir, uint64_t i, char *name, uint32_t *ino_out, uint32_t *type) {
    pent_t *d = of_dir(dir);
    process_t *p = live(d->pid);

    for (int fd = 0; p != NULL && fd < MAX_FDS; fd++) {
        if (p->fds[fd] != NULL && i-- == 0) {
            uitoa((uint64_t)fd, name, 10);
            *ino_out = ino(d->pid, FD_LINK, fd);
            *type = VFS_SYMLINK;
            return 1;
        }
    }
    return 0;
}

static inode_t *pid_find(inode_t *dir, char *name) {
    pent_t *d = of_dir(dir);
    process_t *p = visible(d->pid);

    for (size_t i = 0; p != NULL && i < NPID; i++) {
        if (strcmp(name, pid_entries[i].name) == 0) {
            return owned(get(d->pid, pid_entries[i].kind, pid_entries[i].which, &d->d, name), p);
        }
    }
    return NULL;
}

static int pid_readdir(inode_t *dir, uint64_t i, char *name, uint32_t *ino_out, uint32_t *type) {
    pent_t *d = of_dir(dir);

    if (i >= NPID || visible(d->pid) == NULL) {
        return 0;
    }
    strcpy(name, pid_entries[i].name);
    *ino_out = ino(d->pid, pid_entries[i].kind, pid_entries[i].which);
    *type = pid_entries[i].kind == PID_FILE ? VFS_FILE :
            pid_entries[i].kind == FD_DIR ? VFS_DIRECTORY : VFS_SYMLINK;
    return 1;
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
    for (int i = 0; i < NSYS; i++) {
        if (strcmp(name, sys_names[i]) == 0) {
            return &sys[i];
        }
    }
    if ((pid = number(name)) <= 0 || (p = visible((uint32_t)pid)) == NULL) {
        return NULL;
    }
    return owned(get((uint32_t)pid, PID_DIR, 0, (dentry_t *)root.ptr, name), p);
}

/* self, the system files, then processes by increasing pid. */
static int root_readdir(inode_t *dir, uint64_t i, char *name, uint32_t *ino_out, uint32_t *type) {
    uint32_t prev = 0;

    (void)dir;
    if (i == 0) {
        strcpy(name, "self");
        *ino_out = self.inode;
        *type = VFS_SYMLINK;
        return 1;
    }
    if (i <= NSYS) {
        strcpy(name, sys_names[i - 1]);
        *ino_out = sys[i - 1].inode;
        *type = VFS_FILE;
        return 1;
    }
    for (i -= NSYS + 1;; i--) {
        uint32_t next = 0;
        for (process_t *p = process_list(); p != NULL; p = p->all_next) {
            if (p->pid > prev && (next == 0 || p->pid < next) && visible(p->pid) != NULL) {
                next = p->pid;
            }
        }
        if (next == 0) {
            return 0;
        }
        if (i == 0) {
            uitoa(next, name, 10);
            *ino_out = ino(next, PID_DIR, 0);
            *type = VFS_DIRECTORY;
            return 1;
        }
        prev = next;
    }
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

static void fixed(inode_t *n, const char *name, uint32_t type, uint32_t mode, uint32_t number) {
    vfs_init_inode(n, type, mode);
    n->inode = number;
    n->dev = PROCFS_DEV;
    strcpy(n->name, name);
}

/* The root's dentry, to mount; NULL if /proc is mounted already. */
dentry_t *procfs_mount(void) {
    dentry_t *d;

    if (mounted || (d = k_malloc(sizeof(*d))) == NULL) {
        return NULL;
    }
    if (root.inode == 0) {
        fixed(&root, "proc", VFS_DIRECTORY, 0555, 1);
        root.finddir = root_find;
        root.readdir = root_readdir;
        fixed(&self, "self", VFS_SYMLINK, 0777, 2);
        self.read = self_read;
        for (int i = 0; i < NSYS; i++) {
            fixed(&sys[i], sys_names[i], VFS_FILE, 0444, 3 + (uint32_t)i);
            sys[i].read = sys_read;
        }
    }
    memset(d, 0, sizeof(*d));
    strcpy(d->name, "proc");
    d->inode = &root;
    root.ptr = d;
    mounted = true;
    return d;
}

void procfs_unmounted(void) {
    mounted = false;
}
