#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/fcntl.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>

#define PATH_MAX_ 128
#define IO_MAX    0x7FFFF000L /* Linux caps a single read/write here too */

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

static long fd_install(file_t *f, long min) {
    file_t **fds = scheduler_current()->fds;

    for (long i = min; i < MAX_FDS; i++) {
        if (fds[i] == NULL) {
            fds[i] = f;
            return i;
        }
    }
    file_put(f);
    return -EMFILE;
}

static int node_type(inode_t *n) {
    return n->flags & 0x7;
}

/* Copy a NUL-terminated user string, checking each page it touches. */
static long copy_path(char *dst, long src) {
    for (long i = 0; i < PATH_MAX_; i++) {
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

/* Directory a path is relative to: dirfd, or "/" (no cwd yet). */
static inode_t *base_dir(long dirfd, const char *path, long *err) {
    file_t *f;

    if (path[0] == '/' || dirfd == AT_FDCWD) {
        return fs_root->inode;
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

static long do_read(file_t *f, uint64_t buf, uint64_t len) {
    inode_t *n = f->inode;
    uint32_t r;

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
    r = n->read(n, (uint32_t)f->offset, (uint32_t)len, (uint8_t *)buf);
    f->offset += r;
    return r;
}

static long do_write(file_t *f, uint64_t buf, uint64_t len) {
    inode_t *n = f->inode;
    uint32_t r;

    if ((f->flags & O_ACCMODE) == O_RDONLY) {
        return -EBADF;
    }
    if (n->write == NULL) {
        return -EINVAL;
    }
    if (!vmm_user_ok(buf, len, false)) {
        return -EFAULT;
    }
    if (f->flags & O_APPEND) {
        f->offset = n->length;
    }
    r = n->write(n, (uint32_t)f->offset, (uint32_t)len, (uint8_t *)buf);
    f->offset += r;
    return r;
}

static long do_iov(long fd, long iov, long cnt, bool rd) {
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
    for (long i = 0; i < cnt; i++) {
        uint64_t len = v[i].len > IO_MAX ? IO_MAX : v[i].len;
        long r = rd ? do_read(f, v[i].base, len) : do_write(f, v[i].base, len);
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

static long fill_stat(inode_t *n, long st) {
    struct kstat k;

    if (!vmm_user_ok((uint64_t)st, sizeof(k), true)) {
        return -EFAULT;
    }
    memset(&k, 0, sizeof(k));
    k.dev = 1;
    k.ino = n->inode;
    k.nlink = 1;
    switch (node_type(n)) {
    case VFS_DIRECTORY:
        k.mode = S_IFDIR | 0755;
        break;
    case VFS_CHARDEVICE:
        k.mode = S_IFCHR | 0666;
        k.rdev = (5 << 8) | 1; /* console's Linux major/minor */
        break;
    default:
        k.mode = S_IFREG | 0755;
        break;
    }
    k.size = n->length;
    k.blksize = PAGE_SIZE;
    k.blocks = (n->length + 511) / 512;
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
    return do_iov(fd, iov, cnt, true);
}

long sys_writev(long fd, long iov, long cnt, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return do_iov(fd, iov, cnt, false);
}

long sys_openat(long dirfd, long upath, long flags, long mode, long a5, long a6) {
    (void)mode; (void)a5; (void)a6; /* no permission bits yet */
    char path[PATH_MAX_];
    long err = copy_path(path, upath);
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
    f = file_open(base, path, (uint32_t)flags, &err);
    if (f == NULL) {
        return err;
    }
    return fd_install(f, 0);
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
    file_put(f);
    return 0;
}

long sys_newfstatat(long dirfd, long upath, long st, long flags, long a5, long a6) {
    (void)a5; (void)a6;
    char path[PATH_MAX_];
    long err = copy_path(path, upath);
    inode_t *base, *n;

    if (err < 0) {
        return err;
    }
    if (path[0] == '\0') {
        if (!(flags & AT_EMPTY_PATH)) {
            return -ENOENT;
        }
        return sys_fstat(dirfd, st, 0, 0, 0, 0);
    }
    base = base_dir(dirfd, path, &err);
    if (base == NULL) {
        return err;
    }
    n = vfs_lookup(base, path);
    return n == NULL ? -ENOENT : fill_stat(n, st);
}

/* stat and lstat: no symlinks yet, so they are the same call. */
long sys_stat(long upath, long st, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_newfstatat(AT_FDCWD, upath, st, 0, 0, 0);
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
    if (node_type(f->inode) == VFS_CHARDEVICE) {
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

/* Everyone is root and there are no permission bits: existence is access. */
long sys_faccessat(long dirfd, long upath, long mode, long a4, long a5, long a6) {
    (void)mode; (void)a4; (void)a5; (void)a6;
    char path[PATH_MAX_];
    long err = copy_path(path, upath);
    inode_t *base;

    if (err < 0) {
        return err;
    }
    base = base_dir(dirfd, path, &err);
    if (base == NULL) {
        return err;
    }
    return vfs_lookup(base, path) == NULL ? -ENOENT : 0;
}

long sys_access(long upath, long mode, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_faccessat(AT_FDCWD, upath, mode, 0, 0, 0);
}

long sys_dup(long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    file_t *f = fd_file(fd);

    return f == NULL ? -EBADF : fd_install(file_get(f), 0);
}

long sys_dup3(long oldfd, long newfd, long flags, long a4, long a5, long a6) {
    (void)flags; (void)a4; (void)a5; (void)a6; /* O_CLOEXEC: no exec yet */
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
    return newfd;
}

long sys_dup2(long oldfd, long newfd, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    if (oldfd == newfd) {
        return fd_file(oldfd) == NULL ? -EBADF : newfd;
    }
    return sys_dup3(oldfd, newfd, 0, 0, 0, 0);
}
