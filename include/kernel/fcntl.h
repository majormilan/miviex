#ifndef FCNTL_H
#define FCNTL_H

/* Linux x86-64 values. */
#define O_ACCMODE   0x3
#define O_RDONLY    0x0
#define O_WRONLY    0x1
#define O_RDWR      0x2
#define O_CREAT     0x40
#define O_EXCL      0x80
#define O_TRUNC     0x200
#define O_APPEND    0x400
#define O_NONBLOCK  0x800
#define O_DIRECTORY 0x10000
#define O_CLOEXEC   0x80000

#define F_DUPFD         0
#define F_GETFD         1
#define F_SETFD         2
#define F_GETFL         3
#define F_SETFL         4
#define F_DUPFD_CLOEXEC 1030
#define FD_CLOEXEC      1

#define AT_FDCWD        (-100)
#define AT_EMPTY_PATH   0x1000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif /* FCNTL_H */
