#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/fcntl.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/hal/clock.h>
#include <kernel/mm/pmm.h>
#include <kernel/proc/cred.h>
#include <kernel/vfs/procfs.h>
#include <kernel/vfs/ramfs.h>
#include <kernel/mm/memory.h>

#define PATH_MAX_ 1024
#define IO_MAX    0x7FFFF000L /* Linux caps a single read/write here too */

#define S_IFIFO 0010000
#define S_IFLNK 0120000
#define S_IFCHR 0020000
#define S_IFDIR 0040000
#define S_IFREG 0100000

/* struct stat as the x86-64 kernel lays it out. */
struct kstat {
    uint64_t dev, ino, nlink;
    uint32_t mode, uid, gid, pad0;
    uint64_t rdev;
    int64_t size, blksize, blocks;
    uint64_t atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
    int64_t unused[3];
};

struct iovec {
    uint64_t base;
    uint64_t len;
};

static file_t *fd_file(long fd) {
    if (fd < 0 || fd >= MAX_FDS) {
        return NULL;
    }
    return scheduler_current()->fds[fd];
}

static long fd_install(file_t *f, long min, bool cloexec) {
    process_t *p = scheduler_current();
    long limit = p->rlim[7].cur < MAX_FDS ? (long)p->rlim[7].cur : MAX_FDS; /* RLIMIT_NOFILE */

    for (long i = min < 0 ? 0 : min; i < limit; i++) {
        if (p->fds[i] == NULL) {
            p->fds[i] = f;
            p->cloexec[i] = cloexec;
            return i;
        }
    }
    file_put(f);
    return -EMFILE;
}

static int node_type(inode_t *n) {
    return n->flags & VFS_TYPE;
}

long copy_user_str(char *dst, long src, size_t max) {
    for (size_t i = 0; i < max; i++) {
        uint64_t a = (uint64_t)src + i;
        if ((i == 0 || (a & (PAGE_SIZE - 1)) == 0) && !vmm_user_ok(a, 1, false)) {
            return -EFAULT;
        }
        dst[i] = *(const char *)a;
        if (dst[i] == '\0') {
            return 0;
        }
    }
    return -ENAMETOOLONG;
}

inode_t *cwd_inode(void) {
    inode_t *cwd = scheduler_current()->cwd;

    return cwd != NULL ? cwd : fs_root->inode;
}

/* Directory a path is relative to: dirfd or the cwd. */
static inode_t *base_dir(long dirfd, const char *path, long *err) {
    file_t *f;

    if (path[0] == '/') {
        return fs_root->inode;
    }
    if (dirfd == AT_FDCWD) {
        return cwd_inode();
    }
    f = fd_file(dirfd);
    if (f == NULL) {
        *err = -EBADF;
        return NULL;
    }
    if (node_type(f->inode) != VFS_DIRECTORY) {
        *err = -ENOTDIR;
        return NULL;
    }
    return f->inode;
}

/* The inode a dirfd-relative user path names. */
static inode_t *at_inode(long dirfd, long upath, bool follow, long *err) {
    char path[PATH_MAX_];
    inode_t *base;

    if ((*err = copy_user_str(path, upath, sizeof(path))) < 0) {
        return NULL;
    }
    if ((base = base_dir(dirfd, path, err)) == NULL) {
        return NULL;
    }
    return vfs_resolve(base, path, follow, err);
}

/* The directory holding a dirfd-relative user path, and the last name. */
static inode_t *at_parent(long dirfd, long upath, char name[NAME_MAX + 1], long *err) {
    char path[PATH_MAX_];
    inode_t *base;

    if ((*err = copy_user_str(path, upath, sizeof(path))) < 0) {
        return NULL;
    }
    if (path[0] == '\0') {
        *err = -ENOENT;
        return NULL;
    }
    if ((base = base_dir(dirfd, path, err)) == NULL) {
        return NULL;
    }
    return vfs_parent(base, path, name, err);
}

/* At the file offset, or at *pos (pread/pwrite) leaving it alone. */
static long do_read_at(file_t *f, uint64_t buf, uint64_t len, uint64_t *pos) {
    inode_t *n = f->inode;
    uint64_t *off = pos != NULL ? pos : &f->offset;
    long r;

    if ((f->flags & O_ACCMODE) == O_WRONLY) {
        return -EBADF;
    }
    if (node_type(n) == VFS_DIRECTORY) {
        return -EISDIR;
    }
    if (n->read == NULL) {
        return -EINVAL;
    }
    if (!vmm_user_ok(buf, len, true)) {
        return -EFAULT;
    }
    if (len == 0) {
        return 0; /* without waiting for data, as on Linux */
    }
    r = n->read(n, (uint32_t)*off, (uint32_t)len, (uint8_t *)buf);
    if (r > 0) {
        *off += (uint64_t)r;
    }
    return r;
}

static long do_read(file_t *f, uint64_t buf, uint64_t len) {
    return do_read_at(f, buf, len, NULL);
}

static long do_write_at(file_t *f, uint64_t buf, uint64_t len, uint64_t *pos) {
    inode_t *n = f->inode;
    uint64_t *off = pos != NULL ? pos : &f->offset;
    long r;

    if ((f->flags & O_ACCMODE) == O_RDONLY) {
        return -EBADF;
    }
    if (n->write == NULL) {
        return -EINVAL;
    }
    if (!vmm_user_ok(buf, len, false)) {
        return -EFAULT;
    }
    if ((f->flags & O_APPEND) && pos == NULL) {
        f->offset = n->length;
    }
    r = n->write(n, (uint32_t)*off, (uint32_t)len, (uint8_t *)buf);
    if (r == -EPIPE) {
        signal_send(scheduler_current(), SIGPIPE);
    }
    if (r > 0) {
        *off += (uint64_t)r;
    }
    return r;
}

static long do_write(file_t *f, uint64_t buf, uint64_t len) {
    return do_write_at(f, buf, len, NULL);
}

/* writev on a pipe: gathered into one write, so that up to PIPE_BUF bytes
 * stay atomic however many pieces they come in. */
#define PIPE_BUF 4096
static long pipe_writev(file_t *f, const struct iovec *v, long cnt) {
    uint64_t total = 0, at = 0;
    uint8_t *buf;
    long r;

    for (long i = 0; i < cnt; i++) {
        if (!vmm_user_ok(v[i].base, v[i].len, false)) {
            return -EFAULT;
        }
        total += v[i].len;
    }
    if ((f->flags & O_ACCMODE) == O_RDONLY) {
        return -EBADF;
    }
    if (total > PIPE_BUF) {
        for (long i = 0; i < cnt; i++) {
            if ((r = do_write_at(f, v[i].base, v[i].len, NULL)) < 0) {
                return at ? (long)at : r;
            }
            at += (uint64_t)r;
            if ((uint64_t)r < v[i].len) {
                break;
            }
        }
        return (long)at;
    }
    if (total == 0) {
        return 0;
    }
    if ((buf = k_malloc(total)) == NULL) {
        return -ENOMEM;
    }
    for (long i = 0; i < cnt; i++) {
        memcpy(buf + at, (void *)v[i].base, v[i].len);
        at += v[i].len;
    }
    r = f->inode->write(f->inode, 0, (uint32_t)total, buf);
    k_free(buf);
    if (r == -EPIPE) {
        signal_send(scheduler_current(), SIGPIPE);
    }
    return r;
}

static long do_iov(long fd, long iov, long cnt, bool rd, long pos) {
    file_t *f = fd_file(fd);
    const struct iovec *v = (const struct iovec *)iov;
    long total = 0;

    if (f == NULL) {
        return -EBADF;
    }
    if (cnt < 0 || cnt > 1024) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)iov, (uint64_t)cnt * sizeof(*v), false)) {
        return -EFAULT;
    }
    uint64_t at = (uint64_t)pos, *p = pos >= 0 ? &at : NULL;
    if (p != NULL && (node_type(f->inode) == VFS_PIPE || node_type(f->inode) == VFS_CHARDEVICE)) {
        return -ESPIPE;
    }
    if (!rd && node_type(f->inode) == VFS_PIPE) {
        return pipe_writev(f, v, cnt);
    }
    for (long i = 0; i < cnt; i++) {
        uint64_t len = v[i].len > IO_MAX ? IO_MAX : v[i].len;
        long r = rd ? do_read_at(f, v[i].base, len, p) : do_write_at(f, v[i].base, len, p);
        if (r < 0) {
            return total ? total : r;
        }
        total += r;
        if ((uint64_t)r < len) {
            break;
        }
    }
    return total;
}

static const uint32_t type_bits[] = {
    [VFS_FILE] = S_IFREG, [VFS_DIRECTORY] = S_IFDIR, [VFS_CHARDEVICE] = S_IFCHR,
    [VFS_PIPE] = S_IFIFO, [VFS_SYMLINK] = S_IFLNK,
};

/* Linux counts a directory's "." and its subdirectories' "..". */
static uint64_t links(inode_t *n) {
    uint64_t k = 2;

    if (node_type(n) != VFS_DIRECTORY) {
        return n->nlink;
    }
    for (dentry_t *c = ((dentry_t *)n->ptr)->first_child; c != NULL; c = c->next_sibling) {
        k += node_type(c->inode) == VFS_DIRECTORY;
    }
    return k;
}

static long fill_stat(inode_t *n, long st) {
    struct kstat k;

    if (!vmm_user_ok((uint64_t)st, sizeof(k), true)) {
        return -EFAULT;
    }
    memset(&k, 0, sizeof(k));
    k.dev = n->dev;
    k.ino = n->inode;
    k.nlink = links(n);
    k.mode = type_bits[node_type(n)] | n->mode;
    k.uid = n->uid;
    k.gid = n->gid;
    k.rdev = n->rdev;
    k.size = n->length;
    k.blksize = PAGE_SIZE;
    k.blocks = (n->length + 511) / 512;
    k.atime = n->atime / 1000000000ULL; k.atime_ns = n->atime % 1000000000ULL;
    k.mtime = n->mtime / 1000000000ULL; k.mtime_ns = n->mtime % 1000000000ULL;
    k.ctime = n->ctime / 1000000000ULL; k.ctime_ns = n->ctime % 1000000000ULL;
    *(struct kstat *)st = k;
    return 0;
}

long sys_read(long fd, long buf, long len, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    if (f == NULL) {
        return -EBADF;
    }
    if (len < 0) {
        return -EINVAL;
    }
    return do_read(f, (uint64_t)buf, len > IO_MAX ? IO_MAX : (uint64_t)len);
}

long sys_write(long fd, long buf, long len, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    if (f == NULL) {
        return -EBADF;
    }
    if (len < 0) {
        return -EINVAL;
    }
    return do_write(f, (uint64_t)buf, len > IO_MAX ? IO_MAX : (uint64_t)len);
}

long sys_readv(long fd, long iov, long cnt, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return do_iov(fd, iov, cnt, true, -1);
}

long sys_writev(long fd, long iov, long cnt, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return do_iov(fd, iov, cnt, false, -1);
}

long sys_openat(long dirfd, long upath, long flags, long mode, long a5, long a6) {
    (void)a5; (void)a6;
    char path[PATH_MAX_];
    long err = copy_user_str(path, upath, sizeof(path));
    inode_t *base;
    file_t *f;

    if (err < 0) {
        return err;
    }
    if (path[0] == '\0') {
        return -ENOENT;
    }
    base = base_dir(dirfd, path, &err);
    if (base == NULL) {
        return err;
    }
    f = file_open(base, path, (uint32_t)flags,
                  (uint32_t)mode & ~scheduler_current()->umask, &err);
    if (f == NULL) {
        return err;
    }
    return fd_install(f, 0, flags & O_CLOEXEC);
}

long sys_open(long upath, long flags, long mode, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_openat(AT_FDCWD, upath, flags, mode, 0, 0);
}

long sys_close(long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    if (f == NULL) {
        return -EBADF;
    }
    scheduler_current()->fds[fd] = NULL;
    scheduler_current()->cloexec[fd] = 0;
    file_put(f);
    return 0;
}

long sys_newfstatat(long dirfd, long upath, long st, long flags, long a5, long a6) {
    (void)a5; (void)a6;
    inode_t *n;
    long err;

    if ((flags & AT_EMPTY_PATH) && vmm_user_ok((uint64_t)upath, 1, false) &&
        *(const char *)upath == '\0') {
        return sys_fstat(dirfd, st, 0, 0, 0, 0);
    }
    n = at_inode(dirfd, upath, !(flags & AT_SYMLINK_NOFOLLOW), &err);
    return n == NULL ? err : fill_stat(n, st);
}

long sys_stat(long upath, long st, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_newfstatat(AT_FDCWD, upath, st, 0, 0, 0);
}

long sys_lstat(long upath, long st, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_newfstatat(AT_FDCWD, upath, st, AT_SYMLINK_NOFOLLOW, 0, 0);
}

long sys_fstat(long fd, long st, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    return f == NULL ? -EBADF : fill_stat(f->inode, st);
}

long sys_lseek(long fd, long off, long whence, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);
    long pos;

    if (f == NULL) {
        return -EBADF;
    }
    if (node_type(f->inode) == VFS_CHARDEVICE || node_type(f->inode) == VFS_PIPE) {
        return -ESPIPE;
    }
    switch (whence) {
    case SEEK_SET: pos = off; break;
    case SEEK_CUR: pos = (long)f->offset + off; break;
    case SEEK_END: pos = (long)f->inode->length + off; break;
    default: return -EINVAL;
    }
    if (pos < 0 || pos > 0xFFFFFFFFL) {
        return -EINVAL;
    }
    f->offset = (uint64_t)pos;
    return pos;
}

long sys_ioctl(long fd, long req, long arg, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    if (f == NULL) {
        return -EBADF;
    }
    if (f->inode->ioctl == NULL) {
        return -ENOTTY;
    }
    return f->inode->ioctl(f->inode, (unsigned long)req, (uint64_t)arg);
}

/* access() answers for the real ids, so setuid programs can ask on
 * behalf of their caller. */
long sys_faccessat(long dirfd, long upath, long mode, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    long err;
    inode_t *n;

    if (mode & ~7L) {
        return -EINVAL;
    }
    if ((n = at_inode(dirfd, upath, true, &err)) == NULL) {
        return err;
    }
    return mode == 0 ? 0 : cred_access(n, (int)mode);
}

long sys_access(long upath, long mode, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_faccessat(AT_FDCWD, upath, mode, 0, 0, 0);
}

long sys_dup(long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    return f == NULL ? -EBADF : fd_install(file_get(f), 0, false);
}

long sys_dup3(long oldfd, long newfd, long flags, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(oldfd);
    file_t **fds = scheduler_current()->fds;

    if (f == NULL || newfd < 0 || newfd >= MAX_FDS) {
        return -EBADF;
    }
    if (oldfd == newfd) {
        return -EINVAL;
    }
    file_put(fds[newfd]);
    fds[newfd] = file_get(f);
    scheduler_current()->cloexec[newfd] = (flags & O_CLOEXEC) != 0;
    return newfd;
}

long sys_dup2(long oldfd, long newfd, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (oldfd == newfd) {
        return fd_file(oldfd) == NULL ? -EBADF : newfd;
    }
    return sys_dup3(oldfd, newfd, 0, 0, 0, 0);
}

long sys_fcntl(long fd, long cmd, long arg, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();
    file_t *f = fd_file(fd);

    if (f == NULL) {
        return -EBADF;
    }
    switch (cmd) {
    case F_DUPFD:
    case F_DUPFD_CLOEXEC:
        if (arg < 0 || arg >= MAX_FDS) {
            return -EINVAL;
        }
        return fd_install(file_get(f), arg, cmd == F_DUPFD_CLOEXEC);
    case F_GETFD:
        return p->cloexec[fd] ? FD_CLOEXEC : 0;
    case F_SETFD:
        p->cloexec[fd] = (arg & FD_CLOEXEC) != 0;
        return 0;
    case F_GETFL:
        return f->flags & (O_ACCMODE | O_APPEND | O_NONBLOCK);
    case F_SETFL:
        f->flags = (f->flags & ~(uint32_t)(O_APPEND | O_NONBLOCK)) |
                   (arg & (O_APPEND | O_NONBLOCK));
        return 0;
    }
    return -EINVAL;
}

#define DT_CHR  2
#define DT_DIR  4
#define DT_REG  8
#define DT_FIFO 1
#define DT_LNK  10

static uint8_t dtype_of(uint32_t type) {
    switch (type) {
    case VFS_DIRECTORY:  return DT_DIR;
    case VFS_CHARDEVICE: return DT_CHR;
    case VFS_PIPE:       return DT_FIFO;
    case VFS_SYMLINK:    return DT_LNK;
    }
    return DT_REG;
}

static uint8_t dtype(inode_t *n) {
    return dtype_of(n->flags & VFS_TYPE);
}

/*
 * Directory position: 0 is ".", 1 is "..", then a child's position is
 * its dentry's seq, so removing entries does not move the others. For
 * generated directories (/proc) it is an entry index. Directory inodes
 * keep their dentry in ->ptr.
 * getdents64 records: ino, off, reclen, type, name. The legacy getdents
 * (dietlibc) has the type in the record's last byte instead.
 */
static long do_getdents(long fd, long dirp, long count, bool legacy) {
    file_t *f = fd_file(fd);
    dentry_t *dir, *child;
    long pos = 0;

    if (f == NULL) {
        return -EBADF;
    }
    if (node_type(f->inode) != VFS_DIRECTORY || f->inode->ptr == NULL) {
        return -ENOTDIR;
    }
    if (count < 0 || !vmm_user_ok((uint64_t)dirp, (uint64_t)count, true)) {
        return -EFAULT;
    }
    dir = (dentry_t *)f->inode->ptr;

    for (;;) {
        uint64_t idx = f->offset, next = idx + 1;
        const char *name;
        inode_t *n;
        char gen_name[NAME_MAX + 1];
        uint32_t gen_ino = 0, gen_type = 0;

        if (idx == 0) {
            name = ".";
            n = f->inode;
        } else if (idx == 1) {
            name = "..";
            n = dir->parent != NULL ? dir->parent->inode : f->inode;
        } else if (f->inode->readdir != NULL) {
            uint32_t vtype;
            if (!f->inode->readdir(f->inode, idx - 2, gen_name, &gen_ino, &vtype)) {
                break;
            }
            name = gen_name;
            n = NULL;
            gen_type = vtype;
        } else {
            child = dir->first_child;
            while (child != NULL && child->seq < idx) {
                child = child->next_sibling;
            }
            if (child == NULL) {
                break;
            }
            name = child->name;
            n = child->inode;
            next = child->seq + 1;
        }

        size_t len = strlen(name);
        size_t head = legacy ? 18 : 19;
        uint16_t reclen = (uint16_t)((head + len + 1 + (legacy ? 1 : 0) + 7) & ~7UL);
        if (pos + reclen > count) {
            if (pos == 0) {
                return -EINVAL;
            }
            break;
        }
        uint8_t *e = (uint8_t *)dirp + pos;
        memset(e, 0, reclen);
        *(uint64_t *)e = n != NULL ? n->inode : gen_ino; /* d_ino */
        *(int64_t *)(e + 8) = (int64_t)next; /* d_off: next position */
        *(uint16_t *)(e + 16) = reclen;
        e[legacy ? reclen - 1 : 18] = n != NULL ? dtype(n) : dtype_of(gen_type);
        memcpy(e + head, name, len);
        pos += reclen;
        f->offset = next;
    }
    return pos;
}

long sys_getdents64(long fd, long dirp, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return do_getdents(fd, dirp, count, false);
}

long sys_getdents(long fd, long dirp, long count, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return do_getdents(fd, dirp, count, true);
}

long sys_pipe2(long ufds, long flags, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *rd, *wr;
    long r, w, err;

    if (!vmm_user_ok((uint64_t)ufds, 2 * sizeof(int), true)) {
        return -EFAULT;
    }
    if ((err = pipe_create(&rd, &wr)) < 0) {
        return err;
    }
    r = fd_install(rd, 0, flags & O_CLOEXEC);
    if (r < 0) {
        file_put(wr);
        return r;
    }
    w = fd_install(wr, 0, flags & O_CLOEXEC);
    if (w < 0) {
        sys_close(r, 0, 0, 0, 0, 0);
        return w;
    }
    ((int *)ufds)[0] = (int)r;
    ((int *)ufds)[1] = (int)w;
    return 0;
}

long sys_pipe(long ufds, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_pipe2(ufds, 0, 0, 0, 0, 0);
}

long sys_umask(long mask, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();
    long old = p->umask;

    p->umask = (uint32_t)mask & 0777;
    return old;
}

static long set_mode(inode_t *n, long mode) {
    if (vfs_readonly(n)) {
        return -EROFS;
    }
    if (!cred_owns(n)) {
        return -EPERM;
    }
    if (cred_euid() != 0 && !cred_in_group(n->gid)) {
        mode &= ~(long)S_ISGID;
    }
    n->mode = (uint32_t)mode & 07777;
    vfs_touch(n, VFS_CTIME);
    return 0;
}

long sys_fchmodat(long dirfd, long upath, long mode, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    long err;
    inode_t *n = at_inode(dirfd, upath, true, &err);

    return n == NULL ? err : set_mode(n, mode);
}

long sys_chmod(long upath, long mode, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_fchmodat(AT_FDCWD, upath, mode, 0, 0, 0);
}

long sys_fchmod(long fd, long mode, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    return f == NULL ? -EBADF : set_mode(f->inode, mode);
}

/* -1 leaves an id alone; a chown clears setuid/setgid, as on Linux. Only
 * root gives files away; an owner may pick one of its own groups. */
static long set_owner(inode_t *n, long uid, long gid) {
    if (vfs_readonly(n)) {
        return -EROFS;
    }
    if (cred_euid() != 0 &&
        (!cred_owns(n) || ((int)uid != -1 && (uint32_t)uid != n->uid) ||
         ((int)gid != -1 && !cred_in_group((uint32_t)gid)))) {
        return -EPERM;
    }
    if ((int)uid != -1) {
        n->uid = (uint32_t)uid;
    }
    if ((int)gid != -1) {
        n->gid = (uint32_t)gid;
    }
    if (node_type(n) != VFS_DIRECTORY) {
        n->mode &= ~06000u;
    }
    vfs_touch(n, VFS_CTIME);
    return 0;
}

long sys_fchownat(long dirfd, long upath, long uid, long gid, long flags, long a6) {
    (void)a6;
    long err;
    inode_t *n;

    if ((flags & AT_EMPTY_PATH) && vmm_user_ok((uint64_t)upath, 1, false) &&
        *(const char *)upath == '\0') {
        file_t *f = fd_file(dirfd);
        return f == NULL ? -EBADF : set_owner(f->inode, uid, gid);
    }
    n = at_inode(dirfd, upath, !(flags & AT_SYMLINK_NOFOLLOW), &err);
    return n == NULL ? err : set_owner(n, uid, gid);
}

long sys_chown(long upath, long uid, long gid, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_fchownat(AT_FDCWD, upath, uid, gid, 0, 0);
}

long sys_lchown(long upath, long uid, long gid, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_fchownat(AT_FDCWD, upath, uid, gid, AT_SYMLINK_NOFOLLOW, 0);
}

long sys_fchown(long fd, long uid, long gid, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    return f == NULL ? -EBADF : set_owner(f->inode, uid, gid);
}

#define UTIME_NOW  ((1L << 30) - 1)
#define UTIME_OMIT ((1L << 30) - 2)

/* NULL times: both now. A NULL path means the file dirfd (futimens). */
long sys_utimensat(long dirfd, long upath, long utimes, long flags, long a5, long a6) {
    (void)a5; (void)a6;
    int64_t t[4] = { 0, UTIME_NOW, 0, UTIME_NOW };
    uint64_t *field[2];
    inode_t *n;
    long err;

    if (utimes != 0) {
        if (!vmm_user_ok((uint64_t)utimes, sizeof(t), false)) {
            return -EFAULT;
        }
        memcpy(t, (void *)utimes, sizeof(t));
    }
    if (upath == 0) {
        file_t *f = fd_file(dirfd);
        if (f == NULL) {
            return -EBADF;
        }
        n = f->inode;
    } else if ((n = at_inode(dirfd, upath, !(flags & AT_SYMLINK_NOFOLLOW), &err)) == NULL) {
        return err;
    }
    if (vfs_readonly(n)) {
        return -EROFS;
    }
    /* "now" needs ownership or write access; other times need ownership */
    if (!cred_owns(n)) {
        bool now = (t[1] == UTIME_NOW || t[1] == UTIME_OMIT) &&
                   (t[3] == UTIME_NOW || t[3] == UTIME_OMIT);
        if (!now) {
            return -EPERM;
        }
        if (cred_permission(n, MAY_WRITE) < 0) {
            return -EACCES;
        }
    }
    field[0] = &n->atime;
    field[1] = &n->mtime;
    for (int i = 0; i < 2; i++) {
        int64_t sec = t[2 * i], nsec = t[2 * i + 1];
        if (nsec == UTIME_OMIT) {
            continue;
        }
        if (nsec == UTIME_NOW) {
            *field[i] = clock_realtime_ns();
        } else if (sec < 0 || nsec < 0 || nsec >= 1000000000L) {
            return -EINVAL;
        } else {
            *field[i] = (uint64_t)sec * 1000000000ULL + (uint64_t)nsec;
        }
    }
    vfs_touch(n, VFS_CTIME);
    return 0;
}

long sys_mkdirat(long dirfd, long upath, long mode, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char name[NAME_MAX + 1];
    long err;
    inode_t *dir = at_parent(dirfd, upath, name, &err);

    if (dir == NULL) {
        return err;
    }
    return vfs_create(dir, name, VFS_DIRECTORY,
                      (uint32_t)mode & ~scheduler_current()->umask & 07777, NULL);
}

long sys_mkdir(long upath, long mode, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_mkdirat(AT_FDCWD, upath, mode, 0, 0, 0);
}

#define AT_REMOVEDIR 0x200

long sys_unlinkat(long dirfd, long upath, long flags, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char name[NAME_MAX + 1];
    long err;
    inode_t *dir = at_parent(dirfd, upath, name, &err);

    if (dir == NULL) {
        return err == -EEXIST ? -EBUSY : err; /* "/" */
    }
    return flags & AT_REMOVEDIR ? vfs_rmdir(dir, name) : vfs_unlink(dir, name);
}

long sys_unlink(long upath, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_unlinkat(AT_FDCWD, upath, 0, 0, 0, 0);
}

long sys_rmdir(long upath, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_unlinkat(AT_FDCWD, upath, AT_REMOVEDIR, 0, 0, 0);
}

#define RENAME_NOREPLACE 1

long sys_renameat2(long odirfd, long uold, long ndirfd, long unew, long flags, long a6) {
    (void)a6;
    char oname[NAME_MAX + 1], nname[NAME_MAX + 1];
    long err;
    inode_t *odir, *ndir;

    if (flags & ~RENAME_NOREPLACE) {
        return -EINVAL;
    }
    if ((odir = at_parent(odirfd, uold, oname, &err)) == NULL ||
        (ndir = at_parent(ndirfd, unew, nname, &err)) == NULL) {
        return err == -EEXIST ? -EBUSY : err;
    }
    return vfs_rename(odir, oname, ndir, nname, flags & RENAME_NOREPLACE);
}

long sys_renameat(long odirfd, long uold, long ndirfd, long unew, long a5, long a6) {
    (void)a5; (void)a6;
    return sys_renameat2(odirfd, uold, ndirfd, unew, 0, 0);
}

long sys_rename(long uold, long unew, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_renameat2(AT_FDCWD, uold, AT_FDCWD, unew, 0, 0);
}

#define AT_SYMLINK_FOLLOW 0x400

long sys_linkat(long odirfd, long uold, long ndirfd, long unew, long flags, long a6) {
    (void)a6;
    char name[NAME_MAX + 1];
    long err;
    inode_t *target, *dir;

    if ((target = at_inode(odirfd, uold, flags & AT_SYMLINK_FOLLOW, &err)) == NULL ||
        (dir = at_parent(ndirfd, unew, name, &err)) == NULL) {
        return err;
    }
    return vfs_link(dir, name, target);
}

long sys_link(long uold, long unew, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_linkat(AT_FDCWD, uold, AT_FDCWD, unew, 0, 0);
}

long sys_symlinkat(long utarget, long dirfd, long upath, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char target[PATH_MAX_], name[NAME_MAX + 1];
    long err;
    inode_t *dir, *n;

    if ((err = copy_user_str(target, utarget, sizeof(target))) < 0) {
        return err;
    }
    if (target[0] == '\0') {
        return -ENOENT;
    }
    if ((dir = at_parent(dirfd, upath, name, &err)) == NULL) {
        return err;
    }
    if ((err = vfs_create(dir, name, VFS_SYMLINK, 0777, &n)) < 0) {
        return err;
    }
    err = n->write(n, 0, (uint32_t)strlen(target), (uint8_t *)target);
    return err < 0 ? err : 0;
}

long sys_symlink(long utarget, long upath, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_symlinkat(utarget, AT_FDCWD, upath, 0, 0, 0);
}

long sys_readlinkat(long dirfd, long upath, long ubuf, long size, long a5, long a6) {
    (void)a5; (void)a6;
    long err;
    inode_t *n = at_inode(dirfd, upath, false, &err);

    if (n == NULL) {
        return err;
    }
    if (node_type(n) != VFS_SYMLINK) {
        return -EINVAL;
    }
    if (size <= 0) {
        return -EINVAL;
    }
    if ((uint64_t)size > n->length) {
        size = n->length;
    }
    if (!vmm_user_ok((uint64_t)ubuf, (uint64_t)size, true)) {
        return -EFAULT;
    }
    return n->read(n, 0, (uint32_t)size, (uint8_t *)ubuf); /* not NUL-terminated */
}

long sys_readlink(long upath, long ubuf, long size, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_readlinkat(AT_FDCWD, upath, ubuf, size, 0, 0);
}

static long set_length(inode_t *n, long length) {
    if (vfs_readonly(n)) {
        return -EROFS;
    }
    if (length < 0 || length > 0xFFFFFFFFL) {
        return -EINVAL;
    }
    if (node_type(n) == VFS_DIRECTORY) {
        return -EISDIR;
    }
    if (node_type(n) != VFS_FILE || n->truncate == NULL) {
        return -EINVAL;
    }
    return n->truncate(n, (uint32_t)length) < 0 ? -ENOMEM : 0;
}

long sys_truncate(long upath, long length, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    long err;
    inode_t *n = at_inode(AT_FDCWD, upath, true, &err);

    if (n == NULL) {
        return err;
    }
    if ((err = cred_permission(n, MAY_WRITE)) < 0) {
        return err;
    }
    return set_length(n, length);
}

long sys_ftruncate(long fd, long length, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    if (f == NULL || (f->flags & O_ACCMODE) == O_RDONLY) {
        return f == NULL ? -EBADF : -EINVAL;
    }
    return set_length(f->inode, length);
}

long sys_preadv(long fd, long iov, long cnt, long pos, long a5, long a6) {
    (void)a5; (void)a6;
    return pos < 0 ? -EINVAL : do_iov(fd, iov, cnt, true, pos);
}

long sys_pwritev(long fd, long iov, long cnt, long pos, long a5, long a6) {
    (void)a5; (void)a6;
    return pos < 0 ? -EINVAL : do_iov(fd, iov, cnt, false, pos);
}

long sys_pread64(long fd, long buf, long len, long pos, long a5, long a6) {
    (void)a5; (void)a6;
    struct iovec v = { (uint64_t)buf, (uint64_t)len };
    file_t *f = fd_file(fd);

    if (f == NULL) {
        return -EBADF;
    }
    if (len < 0 || pos < 0) {
        return -EINVAL;
    }
    if (node_type(f->inode) == VFS_PIPE || node_type(f->inode) == VFS_CHARDEVICE) {
        return -ESPIPE;
    }
    uint64_t at = (uint64_t)pos;
    return do_read_at(f, v.base, v.len > IO_MAX ? IO_MAX : v.len, &at);
}

long sys_pwrite64(long fd, long buf, long len, long pos, long a5, long a6) {
    (void)a5; (void)a6;
    file_t *f = fd_file(fd);
    uint64_t at = (uint64_t)pos;

    if (f == NULL) {
        return -EBADF;
    }
    if (len < 0 || pos < 0) {
        return -EINVAL;
    }
    if (node_type(f->inode) == VFS_PIPE || node_type(f->inode) == VFS_CHARDEVICE) {
        return -ESPIPE;
    }
    return do_write_at(f, (uint64_t)buf, len > IO_MAX ? IO_MAX : (uint64_t)len, &at);
}

struct pollfd {
    int32_t fd;
    int16_t events, revents;
};

/* Ready bits of every entry; the number of entries with any. */
static long poll_scan(struct pollfd *v, uint64_t n) {
    long ready = 0;

    for (uint64_t i = 0; i < n; i++) {
        file_t *f;
        int r;

        v[i].revents = 0;
        if (v[i].fd < 0) {
            continue;
        }
        if ((f = fd_file(v[i].fd)) == NULL) {
            r = POLLNVAL;
        } else if (f->inode->poll != NULL) {
            r = f->inode->poll(f->inode, f->flags);
        } else {
            r = POLLIN | POLLOUT;
        }
        v[i].revents = (int16_t)(r & (v[i].events | POLLERR | POLLHUP | POLLNVAL));
        ready += v[i].revents != 0;
    }
    return ready;
}

/* timeout_ns < 0 waits forever. Readiness is re-checked every tick. */
static long do_poll(long ufds, long n, int64_t timeout_ns) {
    struct pollfd *v = (struct pollfd *)ufds;
    uint64_t until = clock_monotonic_ns() + (uint64_t)(timeout_ns > 0 ? timeout_ns : 0);
    long ready;

    if (n < 0 || n > MAX_FDS * 4) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)ufds, (uint64_t)n * sizeof(*v), true)) {
        return -EFAULT;
    }
    for (;;) {
        if ((ready = poll_scan(v, (uint64_t)n)) > 0 || timeout_ns == 0 ||
            (timeout_ns > 0 && clock_monotonic_ns() >= until)) {
            return ready;
        }
        if (signal_pending(scheduler_current())) {
            return -EINTR;
        }
        sleep_on((void *)&jiffies);
    }
}

long sys_poll(long ufds, long n, long timeout_ms, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return do_poll(ufds, n, timeout_ms < 0 ? -1 : timeout_ms * 1000000LL);
}

/* With a signal mask, ppoll waits under it: a signal it unblocks ends
 * the wait, and its handler runs before the old mask is back. */
long sys_ppoll(long ufds, long n, long uts, long sigmask, long size, long a6) {
    (void)a6;
    process_t *p = scheduler_current();
    int64_t t[2], ns = -1;
    long r;

    if (uts != 0) {
        if (!vmm_user_ok((uint64_t)uts, sizeof(t), false)) {
            return -EFAULT;
        }
        memcpy(t, (void *)uts, sizeof(t));
        if (t[0] < 0 || t[1] < 0 || t[1] >= 1000000000L) {
            return -EINVAL;
        }
        ns = t[0] * 1000000000LL + t[1];
    }
    if (sigmask != 0) {
        if (size != 8) {
            return -EINVAL;
        }
        if (!vmm_user_ok((uint64_t)sigmask, 8, false)) {
            return -EFAULT;
        }
        signal_mask_temp(p, *(uint64_t *)sigmask);
    }
    r = do_poll(ufds, n, ns);
    if (r != -EINTR) {
        signal_mask_restore(p);
    }
    return r;
}

#define MS_BIND           4096
#define MS_MOVE           8192
#define UMOUNT_NOFOLLOW   8

static uint32_t next_dev = 16; /* tmpfs instances */

/* proc and tmpfs/ramfs can be mounted, and anything remounted read-only
 * or read-write; bind and move mounts are not supported yet. */
long sys_mount(long usrc, long utarget, long utype, long flags, long data, long a6) {
    (void)data; (void)a6;
    char source[64] = "none", type[16];
    mount_t *m;
    dentry_t *root;
    inode_t *t;
    long err;

    if (cred_euid() != 0) {
        return -EPERM;
    }
    if ((t = at_inode(AT_FDCWD, utarget, true, &err)) == NULL) {
        return err;
    }
    if (node_type(t) != VFS_DIRECTORY) {
        return -ENOTDIR;
    }
    m = vfs_mount_of(t->dev);
    if (flags & MS_REMOUNT) {
        if (m == NULL || m->root != t) {
            return -EINVAL;
        }
        m->flags = (m->flags & ~(uint32_t)MS_RDONLY) | ((uint32_t)flags & MS_RDONLY);
        return 0;
    }
    if (flags & (MS_BIND | MS_MOVE)) {
        return -EINVAL;
    }
    if (m != NULL && m->root == t) {
        return -EBUSY; /* already a mountpoint */
    }
    if ((err = copy_user_str(type, utype, sizeof(type))) < 0) {
        return err;
    }
    if (usrc != 0 && (err = copy_user_str(source, usrc, sizeof(source))) < 0) {
        return err;
    }
    if (strcmp(type, "proc") == 0) {
        root = procfs_mount();
    } else if (strcmp(type, "tmpfs") == 0 || strcmp(type, "ramfs") == 0) {
        root = ramfs_init(next_dev++, type[0] == 't' ? 01777 : 0755);
    } else if (strcmp(type, "devtmpfs") == 0) {
        return -EBUSY; /* there is one, on /dev */
    } else {
        return -ENODEV;
    }
    if (root == NULL) {
        return -EBUSY;
    }
    return vfs_mount((dentry_t *)t->ptr, root, source, type, (uint32_t)flags & MS_RDONLY);
}

/* Busy while any process has a file or its cwd there, or something is
 * mounted inside it. Lazy (MNT_DETACH) unmounts are not supported. */
static bool fs_busy(uint32_t dev) {
    for (process_t *p = process_list(); p != NULL; p = p->all_next) {
        if (p->cwd != NULL && p->cwd->dev == dev) {
            return true;
        }
        for (int i = 0; i < MAX_FDS; i++) {
            if (p->fds[i] != NULL && p->fds[i]->inode->dev == dev) {
                return true;
            }
        }
    }
    for (mount_t *m = vfs_mounts(); m != NULL; m = m->next) {
        if (m->covered != NULL && m->covered->dev == dev) {
            return true;
        }
    }
    return false;
}

long sys_umount2(long utarget, long flags, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    mount_t *m;
    inode_t *t;
    long err;
    bool proc;

    if (cred_euid() != 0) {
        return -EPERM;
    }
    if ((t = at_inode(AT_FDCWD, utarget, !(flags & UMOUNT_NOFOLLOW), &err)) == NULL) {
        return err;
    }
    m = vfs_mount_of(t->dev);
    if (m == NULL || m->root != t) {
        return -EINVAL;
    }
    if (fs_busy(t->dev) || strcmp(m->type, "devtmpfs") == 0) {
        return -EBUSY;
    }
    proc = strcmp(m->type, "proc") == 0;
    if ((err = vfs_umount(m)) == 0 && proc) {
        procfs_unmounted();
    }
    return err;
}

struct kstatfs {
    int64_t type, bsize;
    uint64_t blocks, bfree, bavail, files, ffree;
    int32_t fsid[2];
    int64_t namelen, frsize, flags, spare[4];
};

/* RAM-backed filesystems report RAM; proc reports nothing, as on Linux. */
static long fill_statfs(inode_t *n, long ubuf) {
    mount_t *m = vfs_mount_of(n->dev);
    struct kstatfs k;
    pmm_stats_t st;

    if (!vmm_user_ok((uint64_t)ubuf, sizeof(k), true)) {
        return -EFAULT;
    }
    memset(&k, 0, sizeof(k));
    k.bsize = k.frsize = PAGE_SIZE;
    k.namelen = NAME_MAX;
    k.fsid[0] = (int32_t)n->dev;
    k.flags = 0x20 | (m != NULL && (m->flags & MS_RDONLY) ? 1 : 0); /* ST_VALID, ST_RDONLY */
    if (m != NULL && strcmp(m->type, "proc") == 0) {
        k.type = 0x9FA0;
    } else {
        k.type = m != NULL && strcmp(m->type, "rootfs") == 0 ? 0x858458F6 : 0x01021994;
        pmm_get_stats(&st);
        k.blocks = st.total_frames;
        k.bfree = k.bavail = st.free_frames;
    }
    *(struct kstatfs *)ubuf = k;
    return 0;
}

long sys_statfs(long upath, long ubuf, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    long err;
    inode_t *n = at_inode(AT_FDCWD, upath, true, &err);

    return n == NULL ? err : fill_statfs(n, ubuf);
}

long sys_fstatfs(long fd, long ubuf, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    return f == NULL ? -EBADF : fill_statfs(f->inode, ubuf);
}
