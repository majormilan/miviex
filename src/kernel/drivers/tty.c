#include <kernel/drivers/tty.h>
#include <kernel/hal/io.h>
#include <kernel/hal/isr.h>
#include <kernel/hal/apic.h>
#include <kernel/drivers/device.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/proc/signal.h>
#include <kernel/video/vga.h>
#include <kernel/errno.h>
#include <kernel/libc/string.h>
#include <kernel/proc/cred.h>

/* termios bits and control characters (Linux values) */
#define ICRNL  0x100
#define OPOST  0x1
#define ONLCR  0x4
#define ISIG   0x1
#define ICANON 0x2
#define ECHO   0x8
#define ECHOE  0x10
#define ECHOK  0x20
#define IEXTEN 0x8000
#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VKILL  3
#define VEOF   4
#define VTIME  5
#define VMIN   6
#define VSUSP  10

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404
#define TCSBRK     0x5409
#define TCFLSH     0x540B
#define TIOCSCTTY  0x540E
#define TIOCNOTTY  0x5422
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONREAD   0x541B
#define TCSBRKP    0x5425
#define TIOCGSID   0x5429

#define COM1 0x3F8

struct winsize {
    uint16_t row, col, xpixel, ypixel;
};

static void serial_out(char c) {
    serial_write(c);
}

/* A cooked 80x25 terminal, as Linux sets one up. */
static const struct termios sane = {
    .iflag = ICRNL,
    .oflag = OPOST | ONLCR,
    .cflag = 0xF | 0x30 | 0x80, /* B38400 | CS8 | CREAD */
    .lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN,
    .cc = { [VINTR] = 3, [VQUIT] = 0x1C, [VERASE] = 0x7F, [VKILL] = 0x15,
            [VEOF] = 4, [VTIME] = 0, [VMIN] = 1, [VSUSP] = 0x1A },
};

tty_t tty_vga = { .name = "tty1", .out = vga_putc, .rdev = 4 << 8 | 1 };
tty_t tty_serial = { .name = "ttyS0", .out = serial_out, .rdev = 4 << 8 | 64 };
tty_t *tty_console = &tty_vga;

static void output(tty_t *t, char c) {
    if ((t->t.oflag & (OPOST | ONLCR)) == (OPOST | ONLCR) && c == '\n') {
        t->out('\r');
    }
    t->out(c);
}

static void echo(tty_t *t, char c) {
    if (!(t->t.lflag & ECHO)) {
        return;
    }
    if ((unsigned char)c < 32 && c != '\n' && c != '\t') {
        output(t, '^');
        output(t, (char)(c + '@'));
        return;
    }
    output(t, c);
}

static void queue(tty_t *t, const char *s, uint32_t n) {
    for (uint32_t i = 0; i < n && t->in_len < TTY_BUF; i++) {
        t->in[(t->in_head + t->in_len++) % TTY_BUF] = s[i];
    }
    wakeup(t);
}

static void flush_input(tty_t *t) {
    t->in_head = t->in_len = t->line_len = t->eof = 0;
}

void tty_input(tty_t *t, char c) {
    uint8_t *cc = t->t.cc;

    if ((t->t.iflag & ICRNL) && c == '\r') {
        c = '\n';
    }
    if (t->t.lflag & ISIG) {
        int sig = c == cc[VINTR] ? SIGINT : c == cc[VQUIT] ? SIGQUIT :
                  c == cc[VSUSP] ? SIGTSTP : 0;
        if (sig != 0) {
            flush_input(t);
            echo(t, c);
            if (t->pgrp != 0) {
                signal_send_pgrp(t->pgrp, sig);
            }
            return;
        }
    }
    if (!(t->t.lflag & ICANON)) {
        echo(t, c);
        queue(t, &c, 1);
        return;
    }

    if (c == cc[VERASE] || c == '\b') {
        if (t->line_len > 0) {
            t->line_len--;
            if ((t->t.lflag & (ECHO | ECHOE)) == (ECHO | ECHOE)) {
                output(t, '\b'); output(t, ' '); output(t, '\b');
            }
        }
        return;
    }
    if (c == cc[VKILL]) {
        while (t->line_len > 0 && (t->t.lflag & ECHO)) {
            t->line_len--;
            output(t, '\b'); output(t, ' '); output(t, '\b');
        }
        t->line_len = 0;
        return;
    }
    if (c == cc[VEOF]) {
        if (t->line_len == 0) {
            t->eof++;
            wakeup(t);
        } else {
            queue(t, t->line, t->line_len);
            t->line_len = 0;
        }
        return;
    }
    if (t->line_len < TTY_BUF - 1 || c == '\n') {
        t->line[t->line_len++] = c;
        echo(t, c);
    }
    if (c == '\n') {
        queue(t, t->line, t->line_len);
        t->line_len = 0;
    }
}

/*
 * Canonical: one line (or what is left of it) per read; ^D on an empty
 * line reads as end of file. Raw: whatever is there once VMIN (0 or 1) is
 * met; VTIME is not supported.
 */
long tty_read(tty_t *t, uint8_t *buf, uint32_t n) {
    bool canon = t->t.lflag & ICANON;

    for (;;) {
        if (t->in_len > 0) {
            uint32_t k = 0;
            while (k < n && t->in_len > 0) {
                char c = t->in[t->in_head];
                t->in_head = (t->in_head + 1) % TTY_BUF;
                t->in_len--;
                buf[k++] = (uint8_t)c;
                if (canon && c == '\n') {
                    break;
                }
            }
            return k;
        }
        if (canon && t->eof > 0) {
            t->eof--;
            return 0;
        }
        if (!canon && t->t.cc[VMIN] == 0) {
            return 0;
        }
        if (signal_pending(scheduler_current())) {
            return -ERESTARTSYS;
        }
        sleep_on(t);
    }
}

bool tty_readable(tty_t *t) {
    return t->in_len > 0 || ((t->t.lflag & ICANON) ? t->eof > 0 : t->t.cc[VMIN] == 0);
}

long tty_write(tty_t *t, const uint8_t *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        output(t, (char)buf[i]);
    }
    return n;
}

/* The terminal stops being anybody's controlling terminal. */
static void disown(tty_t *t) {
    for (process_t *q = process_list(); q != NULL; q = q->all_next) {
        if (q->ctty == t) {
            q->ctty = NULL;
        }
    }
    t->sid = t->pgrp = 0;
}

long tty_ioctl(tty_t *t, unsigned long req, uint64_t arg) {
    process_t *p = scheduler_current();
    size_t size = req == TCGETS || req == TCSETS || req == TCSETSW || req == TCSETSF ?
                  sizeof(struct termios) : req == TIOCGWINSZ || req == TIOCSWINSZ ?
                  sizeof(struct winsize) : sizeof(int);
    bool out = req == TCGETS || req == TIOCGWINSZ || req == TIOCGPGRP ||
               req == FIONREAD || req == TIOCGSID;

    if (req == TCSBRK || req == TCSBRKP) {
        return 0; /* tcdrain: output is synchronous */
    }
    if (req != TIOCSCTTY && req != TIOCNOTTY && req != TCFLSH && !vmm_user_ok(arg, size, out)) {
        return -EFAULT;
    }
    switch (req) {
    case TCGETS:
        *(struct termios *)arg = t->t;
        return 0;
    case TCSETSF:
        flush_input(t);
        /* fall through */
    case TCSETS:
    case TCSETSW: {
        bool was_canon = t->t.lflag & ICANON;
        t->t = *(struct termios *)arg;
        if (was_canon && !(t->t.lflag & ICANON) && t->line_len > 0) {
            queue(t, t->line, t->line_len); /* a half-typed line becomes input */
            t->line_len = 0;
        }
        return 0;
    }
    case TCFLSH:
        if (arg == 0 || arg == 2) { /* TCIFLUSH, TCIOFLUSH */
            flush_input(t);
        }
        return 0;
    case TIOCGWINSZ:
        *(struct winsize *)arg = (struct winsize){ t->rows, t->cols, 0, 0 };
        return 0;
    case TIOCSWINSZ: {
        struct winsize *w = (struct winsize *)arg;
        if (w->row != t->rows || w->col != t->cols) {
            t->rows = w->row;
            t->cols = w->col;
            if (t->pgrp != 0) {
                signal_send_pgrp(t->pgrp, SIGWINCH);
            }
        }
        return 0;
    }
    case FIONREAD:
        *(int *)arg = (int)t->in_len;
        return 0;
    case TIOCSCTTY:
        /* For a session leader without a terminal. One that belongs to
         * another session can only be taken by root, with arg 1. */
        if (p->ctty == t && t->sid == p->sid) {
            return 0;
        }
        if (p->sid != p->pid || p->ctty != NULL) {
            return -EPERM;
        }
        if (t->sid != 0) {
            if (arg != 1 || cred_euid() != 0) {
                return -EPERM;
            }
            disown(t);
        }
        t->sid = p->sid;
        t->pgrp = p->pgid;
        p->ctty = t;
        return 0;
    case TIOCNOTTY:
        /* A session leader takes its whole session off the terminal. */
        if (p->ctty != t) {
            return -ENOTTY;
        }
        if (p->sid == p->pid) {
            disown(t);
        } else {
            p->ctty = NULL;
        }
        return 0;
    case TIOCGPGRP:
        *(int *)arg = (int)t->pgrp;
        return 0;
    case TIOCSPGRP:
        if (p->ctty != t || p->sid != t->sid) {
            return -ENOTTY;
        }
        t->pgrp = (uint32_t)*(int *)arg;
        return 0;
    case TIOCGSID:
        if (t->sid == 0) {
            return -ENOTTY;
        }
        *(int *)arg = (int)t->sid;
        return 0;
    }
    return -EINVAL;
}

void tty_hangup(tty_t *t) {
    t->t = sane;
    flush_input(t);
    if (t->pgrp != 0) {
        signal_send_pgrp(t->pgrp, SIGHUP);
    }
    wakeup(t);
}

/* Device nodes: ptr is the tty; /dev/tty (ptr NULL) is the caller's
 * controlling terminal. Without one it cannot be opened (ENXIO), and a
 * process that lost its terminal gets EIO. */
static tty_t *dev_tty(inode_t *node) {
    if (node->ptr != NULL) {
        return (tty_t *)node->ptr;
    }
    return scheduler_current()->ctty;
}

static long dev_open(inode_t *node, uint32_t flags) {
    (void)flags;
    return dev_tty(node) == NULL ? -ENXIO : 0;
}

static long dev_read(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    tty_t *t = dev_tty(node);
    return t != NULL ? tty_read(t, buffer, size) : -EIO;
}

static long dev_write(inode_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    tty_t *t = dev_tty(node);
    return t != NULL ? tty_write(t, buffer, size) : -EIO;
}

static long dev_ioctl(inode_t *node, unsigned long req, uint64_t arg) {
    tty_t *t = dev_tty(node);
    return t != NULL ? tty_ioctl(t, req, arg) : -EIO;
}

static int dev_poll(inode_t *node, uint32_t flags) {
    (void)flags;
    tty_t *t = dev_tty(node);
    return t == NULL ? POLLERR : POLLOUT | (tty_readable(t) ? POLLIN : 0);
}

static const chardev_ops_t tty_ops = {
    .read = dev_read, .write = dev_write, .ioctl = dev_ioctl, .poll = dev_poll,
    .open = dev_open,
};

#define GID_TTY 5
#define GID_DIALOUT 20

static void vga_reply(const char *s) {
    while (*s) {
        tty_input(&tty_vga, *s++);
    }
}

static void serial_irq(registers_t *r) {
    (void)r;
    while (inb(COM1 + 5) & 1) {
        tty_input(&tty_serial, (char)inb(COM1));
    }
}

void tty_init(const char *cmdline) {
    tty_vga.t = sane;
    tty_serial.t = sane;
    tty_vga.rows = tty_serial.rows = 25;
    tty_vga.cols = tty_serial.cols = 80;
    vga_set_reply(vga_reply);
    if (cmdline != NULL && strstr(cmdline, "console=ttyS0") != NULL) {
        tty_console = &tty_serial;
    }

    /* Receive interrupts on COM1 (IRQ4), so ^C works while nobody reads. */
    outb(COM1 + 1, 0x01);
    irq_request(4, serial_irq);

    dev_register("console", 5, 1, 0600, 0, &tty_ops, tty_console);
    dev_register("tty", 5, 0, 0666, 0, &tty_ops, NULL);
    dev_register("tty1", 4, 1, 0620, GID_TTY, &tty_ops, &tty_vga);
    dev_register("ttyS0", 4, 64, 0660, GID_DIALOUT, &tty_ops, &tty_serial);
}
