#include <kernel/log.h>
#include <kernel/video/vga.h>
#include <kernel/libc/stdio.h>
#include <stdarg.h>

static void print_tag(log_level_t level) {
    switch (level) {
        case LOG_OK:
            terminal_print_colorful("[  OK  ]", VGA_COLOR_LIGHT_GREEN);
            break;
        case LOG_FAIL:
            terminal_print_colorful("[ FAIL ]", VGA_COLOR_LIGHT_RED);
            break;
        case LOG_INFO:
            terminal_print_colorful("[ INFO ]", VGA_COLOR_LIGHT_CYAN);
            break;
        case LOG_STATUS:
            terminal_print_colorful("[....]", VGA_COLOR_LIGHT_GREY);
            break;
        case LOG_DEBUG:
            terminal_print_colorful("[DEBUG]", VGA_COLOR_MAGENTA);
            break;
    }
    terminal_print(" ");
}

void klog(log_level_t level, const char* subsystem, const char* format, ...) {
    static char buffer[256];
    va_list args;

    va_start(args, format);
    kvprintf(buffer, format, args);
    va_end(args);

    print_tag(level);
    terminal_print_colorful(subsystem, VGA_COLOR_WHITE);
    terminal_print(": ");
    terminal_print(buffer);
    terminal_print("\n");
}

void klog_execute(init_func_t func, const char* subsystem, const char* message) {
    if (func() == 0) {
        klog(LOG_OK, subsystem, message);
    } else {
        klog(LOG_FAIL, subsystem, message);
    }
}
