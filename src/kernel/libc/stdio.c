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

/* A number padded to width: left-justified, or with zeros or spaces. */
static void putnum(out_t *o, uint64_t n, unsigned base, int neg, int width, char pad, int left,
                   const char *digits) {
    char tmp[24];
    int i = 0, len;

    do {
        tmp[i++] = digits[n % base];
        n /= base;
    } while (n);
    len = i + neg;
    if (neg && pad == '0') {
        put(o, '-');
    }
    while (!left && len < width) {
        put(o, pad);
        width--;
    }
    if (neg && pad != '0') {
        put(o, '-');
    }
    while (i--) {
        put(o, tmp[i]);
    }
    while (left && len < width) {
        put(o, ' ');
        width--;
    }
}

/*
 * Supports %d %i %u %x %X %o %p %s %c %% with the - and 0 flags, a field
 * width and an optional l/ll/z length. Always NUL-terminates when
 * size > 0; returns the untruncated length.
 */
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    static const char lower[] = "0123456789abcdef", upper[] = "0123456789ABCDEF";
    out_t o = { buf, size, 0 };
    const char *s;
    int64_t v;
    int lng, width, left, len;
    char pad;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            put(&o, *fmt);
            continue;
        }
        fmt++;
        left = 0;
        pad = ' ';
        for (;; fmt++) {
            if (*fmt == '-') {
                left = 1;
            } else if (*fmt == '0') {
                pad = '0';
            } else {
                break;
            }
        }
        if (left) {
            pad = ' ';
        }
        for (width = 0; *fmt >= '0' && *fmt <= '9'; fmt++) {
            width = width * 10 + (*fmt - '0');
        }
        lng = 0;
        while (*fmt == 'l' || *fmt == 'z') {
            lng = 1;
            fmt++;
        }
        switch (*fmt) {
        case 'd':
        case 'i':
            v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
            putnum(&o, v < 0 ? -(uint64_t)v : (uint64_t)v, 10, v < 0, width, pad, left, lower);
            break;
        case 'u':
            putnum(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, 0, width, pad, left, lower);
            break;
        case 'x':
        case 'X':
            putnum(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, 0, width, pad, left,
                   *fmt == 'x' ? lower : upper);
            break;
        case 'o':
            putnum(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 8, 0, width, pad, left, lower);
            break;
        case 'p':
            put(&o, '0');
            put(&o, 'x');
            putnum(&o, (uintptr_t)va_arg(ap, void *), 16, 0, 0, ' ', 0, lower);
            break;
        case 's':
            s = va_arg(ap, const char *);
            s = s ? s : "(null)";
            for (len = 0; s[len]; len++) {
                ;
            }
            while (!left && len++ < width) {
                put(&o, ' ');
            }
            while (*s) {
                put(&o, *s++);
            }
            while (left && len++ < width) {
                put(&o, ' ');
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
