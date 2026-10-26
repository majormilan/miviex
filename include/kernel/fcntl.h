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
#define O_DIRECTORY 0x10000

#define AT_FDCWD        (-100)
#define AT_EMPTY_PATH   0x1000

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif /* FCNTL_H */
