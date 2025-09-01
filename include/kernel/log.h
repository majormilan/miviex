#ifndef KERNEL_LOG_H
#define KERNEL_LOG_H

typedef enum {
    LOG_INFO,
    LOG_OK,
    LOG_FAIL,
    LOG_STATUS,
    LOG_DEBUG
} log_level_t;

typedef int (*init_func_t)(void);

void klog(log_level_t level, const char* subsystem, const char* format, ...);
void klog_execute(init_func_t func, const char* subsystem, const char* message);

#endif // KERNEL_LOG_H
