#ifndef ERRNO_H
#define ERRNO_H

/* Linux values: syscalls return -errno, which libc turns into errno. */
#define EPERM   1
#define ENOENT  2
#define ESRCH   3
#define EINTR   4
#define EIO     5
#define E2BIG   7
#define ENOEXEC 8
#define EBADF   9
#define ECHILD  10
#define EAGAIN  11
#define ENOMEM  12
#define EACCES  13
#define ENOTTY  25
#define ESPIPE  29
#define EPIPE   32
#define ERANGE  34
#define EFAULT  14
#define EEXIST  17
#define ENOTDIR 20
#define EISDIR  21
#define EMFILE  24
#define EINVAL  22
#define ENAMETOOLONG 36
#define ENOSYS  38


/* Kernel-internal: a blocking call was interrupted by a signal. It is
 * restarted, or turned into EINTR, when the signal is delivered. */
#define ERESTARTSYS 512

#endif /* ERRNO_H */
