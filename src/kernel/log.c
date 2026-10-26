#include <kernel/log.h>
#include <kernel/video/vga.h>
#include <kernel/libc/stdio.h>
#include <kernel/hal/clock.h>
#include <kernel/libc/string.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
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

/*
 * Every message goes to a ring buffer read with syslog(2) (dmesg), in
 * Linux's raw format "<level>[seconds.micro] text". It reaches the
 * console only if its level is below the console level, 7 by default
 * (8 with "debug", 4 with "quiet" on the kernel command line).
 */
#define LOG_BUF (64 * 1024)

static char logbuf[LOG_BUF];
static size_t log_head, log_len;
static int console_level = 7;

static const int syslog_level[] = {
    [LOG_INFO] = 6, [LOG_OK] = 6, [LOG_FAIL] = 3, [LOG_STATUS] = 6, [LOG_DEBUG] = 7,
};

static void log_put(const char *s) {
    for (; *s != '\0'; s++) {
        logbuf[(log_head + log_len) % LOG_BUF] = *s;
        if (log_len < LOG_BUF) {
            log_len++;
        } else {
            log_head = (log_head + 1) % LOG_BUF; /* drop the oldest byte */
        }
    }
}

void klog_init(const char *cmdline) {
    if (strstr(cmdline, "debug") != NULL) {
        console_level = 8;
    } else if (strstr(cmdline, "quiet") != NULL) {
        console_level = 4;
    }
}

void klog(log_level_t level, const char* subsystem, const char* format, ...) {
    static char buffer[256];
    char line[320];
    uint64_t us = clock_monotonic_ns() / 1000;
    va_list args;

    va_start(args, format);
    kvsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ksnprintf(line, sizeof(line), "<%d>[%5lu.%06lu] %s: %s\n", syslog_level[level],
              us / 1000000, us % 1000000, subsystem, buffer);
    log_put(line);
    if (syslog_level[level] >= console_level) {
        return;
    }

    print_tag(level);
    terminal_print_colorful(subsystem, VGA_COLOR_WHITE);
    terminal_print(": ");
    terminal_print(buffer);
    terminal_print("\n");
}

#define SYSLOG_ACTION_CLOSE         0
#define SYSLOG_ACTION_OPEN          1
#define SYSLOG_ACTION_READ          2
#define SYSLOG_ACTION_READ_ALL      3
#define SYSLOG_ACTION_READ_CLEAR    4
#define SYSLOG_ACTION_CLEAR         5
#define SYSLOG_ACTION_CONSOLE_OFF   6
#define SYSLOG_ACTION_CONSOLE_ON    7
#define SYSLOG_ACTION_CONSOLE_LEVEL 8
#define SYSLOG_ACTION_SIZE_UNREAD   9
#define SYSLOG_ACTION_SIZE_BUFFER   10

/* Reading the whole buffer is open to everyone (dmesg_restrict=0);
 * everything else needs root. READ does not block or consume. */
long sys_syslog(long type, long ubuf, long len, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    static int saved_level = 7;
    size_t n;

    if (type != SYSLOG_ACTION_READ_ALL && type != SYSLOG_ACTION_SIZE_BUFFER &&
        scheduler_current()->euid != 0) {
        return -EPERM;
    }
    switch (type) {
    case SYSLOG_ACTION_CLOSE:
    case SYSLOG_ACTION_OPEN:
        return 0;
    case SYSLOG_ACTION_READ:
    case SYSLOG_ACTION_READ_ALL:
    case SYSLOG_ACTION_READ_CLEAR:
        if (len < 0) {
            return -EINVAL;
        }
        if (!vmm_user_ok((uint64_t)ubuf, (uint64_t)len, true)) {
            return -EFAULT;
        }
        /* the newest len bytes, oldest first */
        n = (size_t)len < log_len ? (size_t)len : log_len;
        for (size_t i = 0; i < n; i++) {
            ((char *)ubuf)[i] = logbuf[(log_head + log_len - n + i) % LOG_BUF];
        }
        if (type == SYSLOG_ACTION_READ_CLEAR) {
            log_head = log_len = 0;
        }
        return (long)n;
    case SYSLOG_ACTION_CLEAR:
        log_head = log_len = 0;
        return 0;
    case SYSLOG_ACTION_CONSOLE_OFF:
        saved_level = console_level;
        console_level = 1;
        return 0;
    case SYSLOG_ACTION_CONSOLE_ON:
        console_level = saved_level;
        return 0;
    case SYSLOG_ACTION_CONSOLE_LEVEL:
        if (len < 1 || len > 8) {
            return -EINVAL;
        }
        console_level = (int)len;
        return 0;
    case SYSLOG_ACTION_SIZE_UNREAD:
        return (long)log_len;
    case SYSLOG_ACTION_SIZE_BUFFER:
        return LOG_BUF;
    }
    return -EINVAL;
}

void klog_execute(init_func_t func, const char* subsystem, const char* message) {
    if (func() == 0) {
        klog(LOG_OK, subsystem, message);
    } else {
        klog(LOG_FAIL, subsystem, message);
    }
}
