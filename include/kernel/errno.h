#ifndef ERRNO_H
#define ERRNO_H

/* Linux values: syscalls return -errno, which libc turns into errno. */
#define EPERM   1
#define ENOENT  2
#define EIO     5
#define EBADF   9
#define ENOMEM  12
#define ENOTTY  25
#define ESPIPE  29
#define EFAULT  14
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EMFILE  24
#define EINVAL  22
#define ENAMETOOLONG 36
#define ENOSYS  38

#endif /* ERRNO_H */
