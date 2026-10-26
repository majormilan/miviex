#include <kernel/libc/stdio.h>
#include <stdint.h>

/* Output cursor that silently drops characters past the end of buf. */
typedef struct {
    char *buf;
    size_t size;
    size_t len;
} out_t;

static void put(out_t *o, char c) {
    if (o->len + 1 < o->size) {
        o->buf[o->len] = c;
    }
    o->len++;
}

static void putnum(out_t *o, uint64_t n, unsigned base, int neg) {
    char tmp[20];
    int i = 0;

    if (neg) {
        put(o, '-');
    }
    do {
        tmp[i++] = "0123456789abcdef"[n % base];
        n /= base;
    } while (n);
    while (i--) {
        put(o, tmp[i]);
    }
}

/*
 * Supports %d %i %u %x %p %s %c %% with an optional l/ll/z length.
 * Always NUL-terminates when size > 0; returns the untruncated length.
 */
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    out_t o = { buf, size, 0 };
    const char *s;
    int64_t v;
    int lng;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        fmt++;
        lng = 0;
        while (*fmt == 'l' || *fmt == 'z') {
            lng = 1;
            fmt++;
        }
        switch (*fmt) {
        case 'd':
        case 'i':
            v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            putnum(&o, v < 0 ? -(uint64_t)v : (uint64_t)v, 10, v < 0);
            break;
        case 'u':
            putnum(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, 0);
            break;
        case 'x':
            putnum(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, 0);
            break;
        case 'p':
            put(&o, '0');
            put(&o, 'x');
            putnum(&o, (uintptr_t)va_arg(ap, void *), 16, 0);
            break;
        case 's':
            for (s = va_arg(ap, const char *), s = s ? s : "(null)"; *s; s++) {
                put(&o, *s);
            }
            break;
        case 'c':
            put(&o, (char)va_arg(ap, int));
            break;
        case '%':
            put(&o, '%');
            break;
        case '\0':
            fmt--;
            break;
        default:
            put(&o, '%');
            put(&o, *fmt);
            break;
        }
    }
    if (size) {
        buf[o.len < size ? o.len : size - 1] = '\0';
    }
    return (int)o.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}
