#include <kernel/vfs/vfs.h>
#include <kernel/fcntl.h>
#include <kernel/errno.h>
#include <kernel/mm/memory.h>
#include <kernel/proc/scheduler.h>
#include <kernel/libc/string.h>

#define PIPE_SIZE 4096 /* also PIPE_BUF: writes up to this size are atomic */

typedef struct {
    uint8_t buf[PIPE_SIZE];
    uint32_t head, len;     /* ring buffer */
    uint32_t readers, writers; /* open file descriptions per end */
} pipe_t;

/* Blocks while empty; 0 (EOF) once no writer is left. */
static long pipe_read(inode_t *n, uint32_t off, uint32_t size, uint8_t *dst) {
    (void)off;
    pipe_t *p = n->ptr;
    uint32_t k;

    while (p->len == 0) {
        if (p->writers == 0) {
            return 0;
        }
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(p);
    }
    k = size < p->len ? size : p->len;
    for (uint32_t i = 0; i < k; i++) {
        dst[i] = p->buf[(p->head + i) % PIPE_SIZE];
    }
    p->head = (p->head + k) % PIPE_SIZE;
    p->len -= k;
    wakeup(p);
    return k;
}

/* Blocks while full. Without readers: -EPIPE (the caller raises SIGPIPE).
 * Up to PIPE_SIZE bytes go in at once, never interleaved with another
 * writer's: such a write waits until all of it fits. */
static long pipe_write(inode_t *n, uint32_t off, uint32_t size, uint8_t *src) {
    (void)off;
    pipe_t *p = n->ptr;
    uint32_t done = 0;

    while (size <= PIPE_SIZE && PIPE_SIZE - p->len < size && p->readers != 0) {
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(p);
    }
    while (done < size) {
        if (p->readers == 0) {
            return done ? (long)done : -EPIPE;
        }
        if (p->len == PIPE_SIZE) {
            if (signal_pending(scheduler_current())) {
                return done ? (long)done : -ERESTARTSYS;
            }
            sleep_on(p);
            continue;
        }
        while (done < size && p->len < PIPE_SIZE) {
            p->buf[(p->head + p->len) % PIPE_SIZE] = src[done++];
            p->len++;
        }
        wakeup(p);
    }
    return done;
}

static int pipe_poll(inode_t *n, uint32_t flags) {
    pipe_t *p = n->ptr;

    if ((flags & O_ACCMODE) == O_RDONLY) {
        return (p->len > 0 ? POLLIN : 0) | (p->writers == 0 ? POLLHUP : 0);
    }
    return p->readers == 0 ? POLLERR : p->len < PIPE_SIZE ? POLLOUT : 0;
}

static void pipe_release(inode_t *n, uint32_t flags) {
    pipe_t *p = n->ptr;

    if ((flags & O_ACCMODE) == O_RDONLY) {
        p->readers--;
    } else {
        p->writers--;
    }
    wakeup(p);
    if (p->readers == 0 && p->writers == 0) {
        k_free(p);
        k_free(n);
    }
}

long pipe_create(file_t **rd, file_t **wr) {
    pipe_t *p = k_malloc(sizeof(*p));
    inode_t *n = k_malloc(sizeof(*n));

    if (p == NULL || n == NULL) {
        goto fail;
    }
    memset(p, 0, sizeof(*p));
    memset(n, 0, sizeof(*n));
    strcpy(n->name, "pipe");
    vfs_init_inode(n, VFS_PIPE, 0600);
    n->nlink = 0; /* anonymous */
    n->read = pipe_read;
    n->write = pipe_write;
    n->release = pipe_release;
    n->poll = pipe_poll;
    n->ptr = p;

    *rd = file_alloc(n, O_RDONLY);
    *wr = file_alloc(n, O_WRONLY);
    if (*rd == NULL || *wr == NULL) {
        k_free(*rd);
        k_free(*wr);
        goto fail;
    }
    p->readers = p->writers = 1;
    return 0;
fail:
    k_free(p);
    k_free(n);
    return -ENOMEM;
}
