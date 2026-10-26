#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/fcntl.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/hal/clock.h>

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

    for (long i = min < 0 ? 0 : min; i < MAX_FDS; i++) {
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

/* Everyone is root until ROADMAP phase 2: existence is access. */
long sys_faccessat(long dirfd, long upath, long mode, long a4, long a5, long a6) {
    (void)mode; (void)a4; (void)a5; (void)a6;
    long err;

    return at_inode(dirfd, upath, true, &err) == NULL ? err : 0;
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

static uint8_t dtype(inode_t *n) {
    switch (node_type(n)) {
    case VFS_DIRECTORY:  return DT_DIR;
    case VFS_CHARDEVICE: return DT_CHR;
    case VFS_PIPE:       return DT_FIFO;
    case VFS_SYMLINK:    return DT_LNK;
    }
    return DT_REG;
}

/*
 * Directory position is an entry index: 0 is ".", 1 is "..", then the
 * children in dentry order. Directory inodes keep their dentry in ->ptr.
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
        uint64_t idx = f->offset;
        const char *name;
        inode_t *n;

        if (idx == 0) {
            name = ".";
            n = f->inode;
        } else if (idx == 1) {
            name = "..";
            n = dir->parent != NULL ? dir->parent->inode : f->inode;
        } else {
            child = dir->first_child;
            for (uint64_t i = 2; child != NULL && i < idx; i++) {
                child = child->next_sibling;
            }
            if (child == NULL) {
                break;
            }
            name = child->name;
            n = child->inode;
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
        *(uint64_t *)e = n->inode;           /* d_ino */
        *(int64_t *)(e + 8) = (int64_t)idx + 1; /* d_off: next position */
        *(uint16_t *)(e + 16) = reclen;
        e[legacy ? reclen - 1 : 18] = dtype(n);
        memcpy(e + head, name, len);
        pos += reclen;
        f->offset = idx + 1;
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

/* -1 leaves an id alone; a chown clears setuid/setgid, as on Linux. */
static long set_owner(inode_t *n, long uid, long gid) {
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

    return n == NULL ? err : set_length(n, length);
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
