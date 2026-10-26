#ifndef TTY_H
#define TTY_H

#include <kernel/types.h>
#include <stdint.h>

#define TTY_BUF 1024

/* Linux struct termios (kernel layout). */
struct termios {
    uint32_t iflag, oflag, cflag, lflag;
    uint8_t line;
    uint8_t cc[19];
};

/* A terminal: a line discipline in front of one output device. */
typedef struct tty {
    const char *name;
    void (*out)(char c);     /* put one byte on the device */
    struct termios t;
    char line[TTY_BUF];      /* canonical mode: the line being edited */
    uint32_t line_len;
    char in[TTY_BUF];        /* ready for read() */
    uint32_t in_head, in_len;
    uint32_t eof;            /* pending ^D on an empty line */
    uint32_t sid, pgrp;      /* owning session, foreground process group */
    uint32_t rdev;           /* device number, for /proc/<pid>/stat tty_nr */
} tty_t;

extern tty_t tty_vga;    /* /dev/tty1: VGA text screen + PS/2 keyboard */
extern tty_t tty_serial; /* /dev/ttyS0: COM1 */
extern tty_t *tty_console;

/* Kernel command line console=ttyS0 picks the serial console. */
void tty_init(const char *cmdline);
/* One byte of input, from an interrupt handler or a terminal reply. */
void tty_input(tty_t *t, char c);
long tty_read(tty_t *t, uint8_t *buf, uint32_t n);
long tty_write(tty_t *t, const uint8_t *buf, uint32_t n);
long tty_ioctl(tty_t *t, unsigned long req, uint64_t arg);
bool tty_readable(tty_t *t); /* a read would not block */
/* vhangup: sane settings, no pending input, SIGHUP to the foreground. */
void tty_hangup(tty_t *t);

#endif /* TTY_H */
