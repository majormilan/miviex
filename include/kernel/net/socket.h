#ifndef SOCKET_H
#define SOCKET_H

#include <kernel/vfs/vfs.h>
#include <stdint.h>

/* read(2) and write(2) on a socket: with the open file's O_NONBLOCK. */
long sock_read(file_t *f, uint8_t *buf, uint64_t len);
long sock_write(file_t *f, uint8_t *buf, uint64_t len);

#endif /* SOCKET_H */
