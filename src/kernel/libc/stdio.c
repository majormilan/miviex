#include <kernel/libc/stdio.h>
#include <kernel/libc/string.h>
#include <kernel/libc/stdlib.h>
#include <stdarg.h>

int kvprintf(char *str, const char *format, va_list ap) {
    char *s = str;
    char temp_buf[12]; // For converting numbers

    for (; *format; ++format) {
        if (*format != '%') {
            *s++ = *format;
            continue;
        }

        ++format; // Move past '%'

        switch (*format) {
            case 'd': {
                int i = va_arg(ap, int);
                itoa(i, temp_buf, 10);
                strcpy(s, temp_buf);
                s += strlen(temp_buf);
                break;
            }
            case 'u': {
                unsigned int u = va_arg(ap, unsigned int);
                uitoa(u, temp_buf, 10);
                strcpy(s, temp_buf);
                s += strlen(temp_buf);
                break;
            }
            case 'x': {
                unsigned int u = va_arg(ap, unsigned int);
                uitoa(u, temp_buf, 16);
                strcpy(s, temp_buf);
                s += strlen(temp_buf);
                break;
            }
            case 's': {
                char *str_arg = va_arg(ap, char *);
                if (!str_arg) {
                    str_arg = "(null)";
                }
                strcpy(s, str_arg);
                s += strlen(str_arg);
                break;
            }
            case '%': {
                *s++ = '%';
                break;
            }
            default: {
                *s++ = '%';
                *s++ = *format;
                break;
            }
        }
    }

    *s = '\0';
    return s - str; // Return number of chars written
}

int ksprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int ret = kvprintf(str, format, ap);
    va_end(ap);
    return ret;
}
