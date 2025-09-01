#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <kernel/hal/isr.h>

// Forward declaration for registers_t from isr.h
// typedef struct registers registers_t; // Removed this line

enum process_state {
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

typedef struct process {
    uint32_t pid;
    enum process_state state;
    registers_t registers; // Saved CPU state
    uint64_t kernel_stack_top; // Top of the kernel stack for this process
    struct process* next;
    struct process* prev;
} process_t;

#endif // PROCESS_H
