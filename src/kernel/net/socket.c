/*
 * Sockets. Only AF_UNIX so far: stream and datagram sockets, named by a
 * file in the file system (bind creates it), or unnamed (socketpair, or
 * a client that never binds). The socket calls are written for more
 * families to come; AF_INET will sit next to AF_UNIX here.
 *
 * A socket is an anonymous inode, like a pipe, whose ptr is its sock_t.
 * A stream connection is two sockets pointing at each other, each with
 * a ring buffer of what it received. A listening socket keeps the
 * connections connect() made until accept() takes them. Datagrams are a
 * list of messages on the receiver.
 *
 * Nothing points at a socket that can go away without being told: a
 * connected peer is cleared when its other end closes, and a datagram
 * socket connect()ed to a name looks the name up again on every send.
 */
#include <kernel/net/socket.h>
#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/fcntl.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/cred.h>
#include <kernel/proc/scheduler.h>
#include <kernel/proc/signal.h>
#include <kernel/libc/string.h>

#define AF_UNSPEC 0
#define AF_UNIX   1

#define SOCK_STREAM   1
#define SOCK_DGRAM    2
#define SOCK_NONBLOCK O_NONBLOCK
#define SOCK_CLOEXEC  O_CLOEXEC

#define SOL_SOCKET    1
#define SO_TYPE       3
#define SO_ERROR      4
#define SO_SNDBUF     7
#define SO_RCVBUF     8
#define SO_PASSCRED   16
#define SO_PEERCRED   17
#define SO_ACCEPTCONN 30
#define SO_REUSEADDR  2

#define MSG_PEEK     0x2
#define MSG_DONTWAIT 0x40
#define MSG_NOSIGNAL 0x4000

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define UNIX_PATH 108   /* sizeof sun_path */
#define SOCK_BUF  16384 /* bytes a socket holds before senders wait */
#define BACKLOG   32

enum { SS_UNCONNECTED, SS_LISTENING, SS_CONNECTED };

struct sockaddr_un {
    uint16_t family;
    char path[UNIX_PATH];
};

struct ucred {
    uint32_t pid, uid, gid;
};

struct msghdr {
    uint64_t name;      /* void *msg_name */
    uint32_t namelen;
    uint32_t pad1;
    uint64_t iov;       /* struct iovec * */
    uint64_t iovlen;
    uint64_t control;
    uint64_t controllen;
    int32_t flags;
    uint32_t pad2;
};

struct uiovec {
    uint64_t base;
    uint64_t len;
};

typedef struct msg {            /* one datagram */
    struct msg *next;
    uint32_t len;
    char from[UNIX_PATH];       /* the sender's name, "" if unnamed */
    uint8_t data[];
} msg_t;

typedef struct sock {
    int type, state;
    struct sock *peer;          /* the other end of a connection or pair */
    char peername[UNIX_PATH];   /* what it is connected to, by name */
    char name[UNIX_PATH];       /* what bind gave it, "" if unnamed */
    inode_t *bound;             /* the file bind made, held */
    uint8_t *buf;               /* stream: received bytes, a ring */
    uint32_t head, len;
    msg_t *first, *last;        /* datagram: received messages */
    uint32_t queued;
    struct sock *backlog[BACKLOG]; /* listening: connections to accept */
    int nback, maxback;
    int eof;                    /* nothing more will arrive */
    int rd_shut, wr_shut;       /* shutdown() */
    struct ucred cred;          /* SO_PEERCRED: who is at the other end */
} sock_t;

static struct ucred self(void) {
    process_t *p = scheduler_current();
    return (struct ucred){ p->pid, p->euid, p->egid };
}

static sock_t *sock_new(int type) {
    sock_t *s = k_malloc(sizeof(*s));

    if (s == NULL) {
        return NULL;
    }
    memset(s, 0, sizeof(*s));
    s->type = type;
    if (type == SOCK_STREAM && (s->buf = k_malloc(SOCK_BUF)) == NULL) {
        k_free(s);
        return NULL;
    }
    return s;
}

/* The end of a socket: its peer learns nothing more will come. */
static void sock_free(sock_t *s) {
    if (s->peer != NULL) {
        s->peer->peer = NULL;
        s->peer->eof = 1;
        wakeup(s->peer);
    }
    for (int i = 0; i < s->nback; i++) {
        sock_free(s->backlog[i]);   /* connected, never accepted */
    }
    if (s->bound != NULL) {
        s->bound->ptr = NULL;       /* the file stays; connecting is refused */
        vfs_iput(s->bound);
    }
    while (s->first != NULL) {
        msg_t *m = s->first;
        s->first = m->next;
        k_free(m);
    }
    wakeup(s);
    k_free(s->buf);
    k_free(s);
}

static sock_t *sock_of(file_t *f) {
    return f != NULL && (f->inode->flags & VFS_TYPE) == VFS_SOCKET ? f->inode->ptr : NULL;
}

/* fd to socket, or the errno to return */
static sock_t *fd_sock(long fd, long *err) {
    file_t *f = fd_file(fd);
    sock_t *s = sock_of(f);

    *err = f == NULL ? -EBADF : s == NULL ? -ENOTSOCK : 0;
    return s;
}

static int sock_poll(inode_t *n, uint32_t flags) {
    (void)flags;
    sock_t *s = n->ptr;
    int r = 0;

    if (s->state == SS_LISTENING) {
        return s->nback > 0 ? POLLIN : 0;
    }
    if (s->type == SOCK_DGRAM) {
        return (s->first != NULL ? POLLIN : 0) | POLLOUT;
    }
    if (s->state != SS_CONNECTED) {
        return 0;
    }
    if (s->len > 0 || s->eof || s->rd_shut) {
        r |= POLLIN;
    }
    if (s->peer != NULL && s->peer->len < SOCK_BUF) {
        r |= POLLOUT;
    }
    if (s->peer == NULL && s->eof) {
        r |= POLLHUP;
    }
    return r;
}

static void sock_release(inode_t *n, uint32_t flags) {
    (void)flags;
    if (n->ptr != NULL) {
        sock_free(n->ptr);
        n->ptr = NULL;
    }
}

static void sock_destroy(inode_t *n) {
    k_free(n);
}

/* An open file for s; fd_install takes it from there. */
static file_t *sock_file(sock_t *s, uint32_t flags) {
    inode_t *n = k_malloc(sizeof(*n));
    file_t *f;

    if (n == NULL) {
        return NULL;
    }
    memset(n, 0, sizeof(*n));
    strcpy(n->name, "socket");
    vfs_init_inode(n, VFS_SOCKET, 0777);
    n->nlink = 0; /* anonymous */
    n->uid = cred_euid();
    n->poll = sock_poll;
    n->release = sock_release;
    n->destroy = sock_destroy;
    n->ptr = s;
    if ((f = file_alloc(n, O_RDWR | (flags & O_NONBLOCK))) == NULL) {
        k_free(n);
    }
    return f;
}

static long sock_install(sock_t *s, long type) {
    file_t *f = sock_file(s, (uint32_t)type);

    if (f == NULL) {
        sock_free(s);
        return -ENOMEM;
    }
    return fd_install(f, 0, (type & SOCK_CLOEXEC) != 0);
}

/* A sockaddr_un from user space: its path, NUL-terminated. */
static long get_addr(long uaddr, long len, char path[UNIX_PATH]) {
    struct sockaddr_un a;

    if (len < 2 || (uint64_t)len > sizeof(a)) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)uaddr, (uint64_t)len, false)) {
        return -EFAULT;
    }
    memset(&a, 0, sizeof(a));
    memcpy(&a, (void *)uaddr, (size_t)len);
    if (a.family != AF_UNIX) {
        return -EAFNOSUPPORT;
    }
    if (len == 2 || a.path[0] == '\0') {
        return -EINVAL; /* no abstract names (yet) */
    }
    memcpy(path, a.path, UNIX_PATH - 1);
    path[UNIX_PATH - 1] = '\0';
    return 0;
}

/* Hand a name back, as getsockname and recvfrom do. */
static long put_addr(const char *path, long uaddr, long ulen) {
    struct sockaddr_un a;
    uint32_t len, room;

    if (uaddr == 0 || ulen == 0) {
        return 0;
    }
    if (!vmm_user_ok((uint64_t)ulen, sizeof(uint32_t), true)) {
        return -EFAULT;
    }
    room = *(uint32_t *)ulen;
    memset(&a, 0, sizeof(a));
    a.family = AF_UNIX;
    strcpy(a.path, path);
    len = 2 + (path[0] ? (uint32_t)strlen(path) + 1 : 0);
    if (room > len) {
        room = len;
    }
    if (!vmm_user_ok((uint64_t)uaddr, room, true)) {
        return -EFAULT;
    }
    memcpy((void *)uaddr, &a, room);
    *(uint32_t *)ulen = len;
    return 0;
}

/* The socket bound to path, or an errno; *n is the file, held. */
static sock_t *lookup(const char *path, inode_t **n, long *err) {
    inode_t *base = base_dir(AT_FDCWD, path, err);

    *n = NULL;
    if (base == NULL || (*n = vfs_resolve(base, path, true, err)) == NULL) {
        return NULL;
    }
    if (((*n)->flags & VFS_TYPE) != VFS_SOCKET) {
        *n = NULL;
        *err = -ECONNREFUSED;
        return NULL;
    }
    if ((*err = cred_permission(*n, MAY_WRITE)) < 0) {
        *n = NULL;
        return NULL;
    }
    if ((*n)->ptr == NULL) {
        *n = NULL;
        *err = -ECONNREFUSED;   /* the file of a socket that is gone */
        return NULL;
    }
    vfs_iget(*n);
    return (*n)->ptr;
}

/* Stream: wait for bytes, then copy them out. 0 at the end. */
static long stream_recv(sock_t *s, uint8_t *dst, uint64_t size, int nonblock, int peek) {
    uint32_t k;

    while (s->len == 0) {
        if (s->state != SS_CONNECTED) {
            return -ENOTCONN;
        }
        if (s->eof || s->rd_shut) {
            return 0;
        }
        if (nonblock) {
            return -EAGAIN;
        }
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(s);
    }
    k = size < s->len ? (uint32_t)size : s->len;
    for (uint32_t i = 0; i < k; i++) {
        dst[i] = s->buf[(s->head + i) % SOCK_BUF];
    }
    if (!peek) {
        s->head = (s->head + k) % SOCK_BUF;
        s->len -= k;
        wakeup(s); /* writers wait on the socket they fill */
    }
    return k;
}

static long stream_send(sock_t *s, const uint8_t *src, uint64_t size, int nonblock, int nosig) {
    uint64_t done = 0;

    if (s->state != SS_CONNECTED) {
        return -ENOTCONN;
    }
    while (done < size) {
        sock_t *p = s->peer;
        if (s->wr_shut || p == NULL || p->rd_shut) {
            if (done) {
                break;
            }
            if (!nosig) {
                signal_send(scheduler_current(), SIGPIPE);
            }
            return -EPIPE;
        }
        if (p->len == SOCK_BUF) {
            if (nonblock) {
                return done ? (long)done : -EAGAIN;
            }
            if (signal_pending(scheduler_current())) {
                return done ? (long)done : -ERESTARTSYS;
            }
            sleep_on(p);
            continue;
        }
        while (done < size && p->len < SOCK_BUF) {
            p->buf[(p->head + p->len) % SOCK_BUF] = src[done++];
            p->len++;
        }
        wakeup(p);
    }
    return (long)done;
}

/* The receiver of a datagram: the pair's other end, the name given, or
 * the name connect() gave. *held is the file to let go of after. */
static sock_t *dgram_target(sock_t *s, const char *to, inode_t **held, long *err) {
    sock_t *t;

    *held = NULL;
    if (to == NULL && s->peer != NULL) {
        return s->peer;
    }
    if (to == NULL && s->peername[0] == '\0') {
        *err = s->state == SS_CONNECTED ? -ECONNREFUSED : -ENOTCONN;
        return NULL;
    }
    if ((t = lookup(to != NULL ? to : s->peername, held, err)) == NULL) {
        return NULL;
    }
    if (t->type != SOCK_DGRAM) {
        vfs_iput(*held);
        *held = NULL;
        *err = -EPROTOTYPE;
        return NULL;
    }
    return t;
}

static long dgram_send(sock_t *s, const uint8_t *src, uint64_t size, const char *to,
                       int nonblock) {
    inode_t *held;
    sock_t *t;
    msg_t *m;
    long err;

    if (size > SOCK_BUF) {
        return -EMSGSIZE;
    }
    for (;;) {
        if ((t = dgram_target(s, to, &held, &err)) == NULL) {
            return err;
        }
        if (t->queued + size <= SOCK_BUF) {
            break;
        }
        if (held != NULL) {
            vfs_iput(held);
        }
        if (nonblock) {
            return -EAGAIN;
        }
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(t); /* looked up again after: it may have gone */
    }
    if ((m = k_malloc(sizeof(*m) + size)) == NULL) {
        if (held != NULL) {
            vfs_iput(held);
        }
        return -ENOMEM;
    }
    m->next = NULL;
    m->len = (uint32_t)size;
    strcpy(m->from, s->name);
    memcpy(m->data, src, size);
    if (t->last != NULL) {
        t->last->next = m;
    } else {
        t->first = m;
    }
    t->last = m;
    t->queued += m->len;
    wakeup(t);
    if (held != NULL) {
        vfs_iput(held);
    }
    return (long)size;
}

/* One datagram, cut to size if it is longer. from gets its sender. */
static long dgram_recv(sock_t *s, uint8_t *dst, uint64_t size, char *from, int nonblock,
                       int peek) {
    msg_t *m;
    uint32_t k;

    while ((m = s->first) == NULL) {
        if (s->rd_shut || (s->peername[0] == '\0' && s->eof)) {
            return 0;
        }
        if (nonblock) {
            return -EAGAIN;
        }
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(s);
    }
    k = size < m->len ? (uint32_t)size : m->len;
    memcpy(dst, m->data, k);
    if (from != NULL) {
        strcpy(from, m->from);
    }
    if (!peek) {
        if ((s->first = m->next) == NULL) {
            s->last = NULL;
        }
        s->queued -= m->len;
        k_free(m);
        wakeup(s);
    }
    return k;
}

static long do_send(sock_t *s, const uint8_t *buf, uint64_t len, const char *to, int flags,
                    uint32_t fflags) {
    int nonblock = (fflags & O_NONBLOCK) || (flags & MSG_DONTWAIT);

    if (s->type == SOCK_STREAM) {
        if (to != NULL) {
            return s->state == SS_CONNECTED ? -EISCONN : -EOPNOTSUPP;
        }
        return stream_send(s, buf, len, nonblock, flags & MSG_NOSIGNAL);
    }
    return dgram_send(s, buf, len, to, nonblock);
}

static long do_recv(sock_t *s, uint8_t *buf, uint64_t len, char *from, int flags,
                    uint32_t fflags) {
    int nonblock = (fflags & O_NONBLOCK) || (flags & MSG_DONTWAIT);

    if (from != NULL) {
        from[0] = '\0';
    }
    if (s->type == SOCK_STREAM) {
        if (from != NULL && s->peer != NULL) {
            strcpy(from, s->peer->name);
        }
        return stream_recv(s, buf, len, nonblock, flags & MSG_PEEK);
    }
    return dgram_recv(s, buf, len, from, nonblock, flags & MSG_PEEK);
}

long sock_read(file_t *f, uint8_t *buf, uint64_t len) {
    return len == 0 ? 0 : do_recv(sock_of(f), buf, len, NULL, 0, f->flags);
}

long sock_write(file_t *f, uint8_t *buf, uint64_t len) {
    return len == 0 ? 0 : do_send(sock_of(f), buf, len, NULL, 0, f->flags);
}

long sys_socket(long family, long type, long proto, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    long t = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    sock_t *s;

    if (family != AF_UNIX) {
        return -EAFNOSUPPORT;
    }
    if (t != SOCK_STREAM && t != SOCK_DGRAM) {
        return -ESOCKTNOSUPPORT;
    }
    if (proto != 0) {
        return -EPROTONOSUPPORT;
    }
    if ((s = sock_new((int)t)) == NULL) {
        return -ENOMEM;
    }
    return sock_install(s, type);
}

long sys_socketpair(long family, long type, long proto, long usv, long a5, long a6) {
    (void)a5; (void)a6;
    long t = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    sock_t *a, *b;
    long fa, fb;

    if (family != AF_UNIX) {
        return -EAFNOSUPPORT;
    }
    if (t != SOCK_STREAM && t != SOCK_DGRAM) {
        return -ESOCKTNOSUPPORT;
    }
    if (proto != 0) {
        return -EPROTONOSUPPORT;
    }
    if (!vmm_user_ok((uint64_t)usv, 2 * sizeof(int), true)) {
        return -EFAULT;
    }
    a = sock_new((int)t);
    b = sock_new((int)t);
    if (a == NULL || b == NULL) {
        if (a != NULL) sock_free(a);
        if (b != NULL) sock_free(b);
        return -ENOMEM;
    }
    a->peer = b;
    b->peer = a;
    a->state = b->state = SS_CONNECTED;
    a->cred = b->cred = self();
    if ((fa = sock_install(a, type)) < 0) {
        sock_free(b);
        return fa;
    }
    if ((fb = sock_install(b, type)) < 0) {
        sys_close(fa, 0, 0, 0, 0, 0);
        return fb;
    }
    ((int *)usv)[0] = (int)fa;
    ((int *)usv)[1] = (int)fb;
    return 0;
}

/* Bind makes the file: a socket node, as the caller's umask allows. */
long sys_bind(long fd, long uaddr, long len, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char path[UNIX_PATH], name[NAME_MAX + 1];
    inode_t *dir, *n;
    sock_t *s;
    long err;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if ((err = get_addr(uaddr, len, path)) < 0) {
        return err;
    }
    if (s->bound != NULL) {
        return -EINVAL;
    }
    if ((dir = base_dir(AT_FDCWD, path, &err)) == NULL ||
        (dir = vfs_parent(dir, path, name, &err)) == NULL) {
        return err;
    }
    err = vfs_create(dir, name, VFS_SOCKET, 0777 & ~scheduler_current()->umask, &n);
    if (err < 0) {
        return err == -EEXIST ? -EADDRINUSE : err;
    }
    n->ptr = s;
    vfs_iget(n);
    s->bound = n;
    strcpy(s->name, path);
    return 0;
}

long sys_listen(long fd, long backlog, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    sock_t *s;
    long err;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (s->type != SOCK_STREAM) {
        return -EOPNOTSUPP;
    }
    if (s->bound == NULL || s->state == SS_CONNECTED) {
        return -EINVAL;
    }
    s->state = SS_LISTENING;
    s->maxback = backlog < 1 ? 1 : backlog > BACKLOG ? BACKLOG : (int)backlog;
    s->cred = self(); /* what connecting clients see as their peer */
    return 0;
}

/* A stream connect is complete when the listener has queued it: the
 * server side exists already, accept() only hands it out. */
static long stream_connect(sock_t *s, const char *path, int nonblock) {
    inode_t *n;
    sock_t *l, *srv;
    long err;

    if (s->state == SS_CONNECTED) {
        return -EISCONN;
    }
    if (s->state == SS_LISTENING) {
        return -EINVAL;
    }
    for (;;) {
        if ((l = lookup(path, &n, &err)) == NULL) {
            return err;
        }
        if (l->type != SOCK_STREAM) {
            vfs_iput(n);
            return -EPROTOTYPE;
        }
        if (l->state != SS_LISTENING) {
            vfs_iput(n);
            return -ECONNREFUSED;
        }
        if (l->nback < l->maxback) {
            break;
        }
        vfs_iput(n);
        if (nonblock) {
            return -EAGAIN;
        }
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(l);
    }
    if ((srv = sock_new(SOCK_STREAM)) == NULL) {
        vfs_iput(n);
        return -ENOMEM;
    }
    srv->state = s->state = SS_CONNECTED;
    srv->peer = s;
    s->peer = srv;
    strcpy(srv->name, l->name);
    strcpy(s->peername, path);
    srv->cred = self();
    s->cred = l->cred;
    l->backlog[l->nback++] = srv;
    wakeup(l);
    vfs_iput(n);
    return 0;
}

long sys_connect(long fd, long uaddr, long len, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char path[UNIX_PATH];
    inode_t *held;
    sock_t *s;
    long err;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (s->type == SOCK_DGRAM && len >= 2 && vmm_user_ok((uint64_t)uaddr, 2, false) &&
        *(uint16_t *)uaddr == AF_UNSPEC) {
        s->peername[0] = '\0';  /* dissolve the association */
        s->state = SS_UNCONNECTED;
        return 0;
    }
    if ((err = get_addr(uaddr, len, path)) < 0) {
        return err;
    }
    if (s->type == SOCK_STREAM) {
        return stream_connect(s, path, (fd_file(fd)->flags & O_NONBLOCK) != 0);
    }
    if (dgram_target(s, path, &held, &err) == NULL) {
        return err;
    }
    vfs_iput(held);
    strcpy(s->peername, path);
    s->state = SS_CONNECTED;
    return 0;
}

long sys_accept4(long fd, long uaddr, long ulen, long flags, long a5, long a6) {
    (void)a5; (void)a6;
    file_t *f = fd_file(fd);
    sock_t *s, *c;
    long err, r;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC)) {
        return -EINVAL;
    }
    if (s->state != SS_LISTENING) {
        return -EINVAL;
    }
    while (s->nback == 0) {
        if (f->flags & O_NONBLOCK) {
            return -EAGAIN;
        }
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(s);
    }
    c = s->backlog[0];
    memmove(s->backlog, s->backlog + 1, (size_t)--s->nback * sizeof(*s->backlog));
    wakeup(s); /* room for another connect */
    if ((r = put_addr(c->peer != NULL ? c->peer->name : "", uaddr, ulen)) < 0) {
        sock_free(c);
        return r;
    }
    return sock_install(c, flags);
}

long sys_accept(long fd, long uaddr, long ulen, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return sys_accept4(fd, uaddr, ulen, 0, 0, 0);
}

long sys_sendto(long fd, long buf, long len, long flags, long uaddr, long alen) {
    char path[UNIX_PATH];
    sock_t *s;
    long err;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (len < 0 || !vmm_user_ok((uint64_t)buf, (uint64_t)len, false)) {
        return -EFAULT;
    }
    if (uaddr != 0 && (err = get_addr(uaddr, alen, path)) < 0) {
        return err;
    }
    return do_send(s, (uint8_t *)buf, (uint64_t)len, uaddr != 0 ? path : NULL, (int)flags,
                   fd_file(fd)->flags);
}

long sys_recvfrom(long fd, long buf, long len, long flags, long uaddr, long ulen) {
    char from[UNIX_PATH];
    sock_t *s;
    long err, r;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (len < 0 || !vmm_user_ok((uint64_t)buf, (uint64_t)len, true)) {
        return -EFAULT;
    }
    r = do_recv(s, (uint8_t *)buf, (uint64_t)len, from, (int)flags, fd_file(fd)->flags);
    if (r >= 0 && (err = put_addr(from, uaddr, ulen)) < 0) {
        return err;
    }
    return r;
}

/* The iovecs of a msghdr: checked, and their total length. */
static long msg_iov(struct msghdr *m, int out, uint64_t *total) {
    struct uiovec *v = (struct uiovec *)m->iov;

    if (m->iovlen > 1024 || !vmm_user_ok(m->iov, m->iovlen * sizeof(*v), false)) {
        return -EFAULT;
    }
    *total = 0;
    for (uint64_t i = 0; i < m->iovlen; i++) {
        if (!vmm_user_ok(v[i].base, v[i].len, out)) {
            return -EFAULT;
        }
        *total += v[i].len;
    }
    return *total > 1 << 20 ? -EMSGSIZE : 0;
}

/* No ancillary data (SCM_RIGHTS, SCM_CREDENTIALS) yet. */
long sys_sendmsg(long fd, long umsg, long flags, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char path[UNIX_PATH];
    struct msghdr *m = (struct msghdr *)umsg;
    struct uiovec *v;
    uint64_t total, at = 0;
    uint8_t *buf;
    sock_t *s;
    long err, r;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (!vmm_user_ok((uint64_t)umsg, sizeof(*m), false)) {
        return -EFAULT;
    }
    if (m->controllen != 0) {
        return -EOPNOTSUPP;
    }
    if ((err = msg_iov(m, 0, &total)) < 0) {
        return err;
    }
    if (m->name != 0 && (err = get_addr((long)m->name, m->namelen, path)) < 0) {
        return err;
    }
    /* gathered first: a datagram is one message */
    if ((buf = k_malloc(total ? total : 1)) == NULL) {
        return -ENOMEM;
    }
    v = (struct uiovec *)m->iov;
    for (uint64_t i = 0; i < m->iovlen; i++) {
        memcpy(buf + at, (void *)v[i].base, v[i].len);
        at += v[i].len;
    }
    r = do_send(s, buf, total, m->name != 0 ? path : NULL, (int)flags, fd_file(fd)->flags);
    k_free(buf);
    return r;
}

long sys_recvmsg(long fd, long umsg, long flags, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char from[UNIX_PATH];
    struct msghdr *m = (struct msghdr *)umsg;
    struct uiovec *v;
    uint64_t total, at = 0;
    uint8_t *buf;
    sock_t *s;
    long err, r;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (!vmm_user_ok((uint64_t)umsg, sizeof(*m), true)) {
        return -EFAULT;
    }
    if ((err = msg_iov(m, 1, &total)) < 0) {
        return err;
    }
    if ((buf = k_malloc(total ? total : 1)) == NULL) {
        return -ENOMEM;
    }
    r = do_recv(s, buf, total, from, (int)flags, fd_file(fd)->flags);
    v = (struct uiovec *)m->iov;
    for (uint64_t i = 0; r > 0 && i < m->iovlen && at < (uint64_t)r; i++) {
        uint64_t k = (uint64_t)r - at < v[i].len ? (uint64_t)r - at : v[i].len;
        memcpy((void *)v[i].base, buf + at, k);
        at += k;
    }
    k_free(buf);
    if (r < 0) {
        return r;
    }
    m->controllen = 0;
    m->flags = 0;
    if (m->name != 0) {
        uint32_t len = m->namelen;
        if ((err = put_addr(from, (long)m->name, (long)&len)) < 0) {
            return err;
        }
        m->namelen = len;
    }
    return r;
}

long sys_shutdown(long fd, long how, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    sock_t *s;
    long err;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (how < SHUT_RD || how > SHUT_RDWR) {
        return -EINVAL;
    }
    if (s->state != SS_CONNECTED) {
        return -ENOTCONN;
    }
    if (how != SHUT_WR) {
        s->rd_shut = 1;
        wakeup(s);
    }
    if (how != SHUT_RD) {
        s->wr_shut = 1;
        if (s->peer != NULL) {
            s->peer->eof = 1;   /* it reads what is left, then 0 */
            wakeup(s->peer);
        }
    }
    return 0;
}

long sys_getsockname(long fd, long uaddr, long ulen, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    sock_t *s;
    long err;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    return put_addr(s->name, uaddr, ulen);
}

long sys_getpeername(long fd, long uaddr, long ulen, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    sock_t *s;
    long err;

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (s->state != SS_CONNECTED) {
        return -ENOTCONN;
    }
    return put_addr(s->peer != NULL && s->peername[0] == '\0' ? s->peer->name : s->peername,
                    uaddr, ulen);
}

long sys_getsockopt(long fd, long level, long opt, long uval, long ulen, long a6) {
    (void)a6;
    struct ucred cred;
    uint32_t room;
    int v;
    sock_t *s;
    long err;
    void *src = &v;
    uint32_t size = sizeof(v);

    if ((s = fd_sock(fd, &err)) == NULL) {
        return err;
    }
    if (level != SOL_SOCKET) {
        return -ENOPROTOOPT;
    }
    switch (opt) {
    case SO_TYPE:       v = s->type; break;
    case SO_ERROR:      v = 0; break;
    case SO_SNDBUF:
    case SO_RCVBUF:     v = SOCK_BUF; break;
    case SO_ACCEPTCONN: v = s->state == SS_LISTENING; break;
    case SO_PEERCRED:
        if (s->state != SS_CONNECTED) {
            return -ENOTCONN;
        }
        cred = s->cred;
        src = &cred;
        size = sizeof(cred);
        break;
    default:
        return -ENOPROTOOPT;
    }
    if (!vmm_user_ok((uint64_t)ulen, sizeof(uint32_t), true)) {
        return -EFAULT;
    }
    room = *(uint32_t *)ulen < size ? *(uint32_t *)ulen : size;
    if (!vmm_user_ok((uint64_t)uval, room, true)) {
        return -EFAULT;
    }
    memcpy((void *)uval, src, room);
    *(uint32_t *)ulen = room;
    return 0;
}

/* Accepted and ignored: nothing here can be tuned yet. */
long sys_setsockopt(long fd, long level, long opt, long uval, long len, long a6) {
    (void)uval; (void)len; (void)a6;
    long err;

    if (fd_sock(fd, &err) == NULL) {
        return err;
    }
    if (level != SOL_SOCKET) {
        return -ENOPROTOOPT;
    }
    switch (opt) {
    case SO_REUSEADDR: case SO_PASSCRED: case SO_SNDBUF: case SO_RCVBUF:
        return 0;
    }
    return -ENOPROTOOPT;
}
