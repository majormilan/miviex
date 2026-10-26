#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

#include <kernel/libc/stdio.h>

typedef enum {
    LOG_INFO,
    LOG_OK,
    LOG_FAIL,
    LOG_STATUS,
    LOG_DEBUG
} log_level_t;

typedef int (*init_func_t)(void);

void klog(log_level_t level, const char* subsystem, const char* format, ...) PRINTF_LIKE(3, 4);
void klog_execute(init_func_t func, const char* subsystem, const char* message);
// Console level from the kernel command line ("debug", "quiet").
void klog_init(const char *cmdline);

#endif // KERNEL_LOG_H
