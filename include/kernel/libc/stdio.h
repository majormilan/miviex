#ifndef KERNEL_STDIO_H
#define KERNEL_STDIO_H

#include <stdarg.h>
#include <stddef.h>

// A simplified vsprintf
int kvprintf(char *str, const char *format, va_list ap);

// A simplified sprintf
int ksprintf(char *str, const char *format, ...);

#endif // KERNEL_STDIO_H
