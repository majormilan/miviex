#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

// Note: intentionally not including <stdbool.h> here — kernel/hal/isr.h (via
// kernel/types.h) already provides bool/true/false, and including both
// causes a `typedef _Bool bool;` redefinition conflict once stdbool.h's
// `bool` macro has already expanded.
#include <kernel/hal/isr.h>

// Forward declaration for registers_t from isr.h
// typedef struct registers registers_t; // Removed this line

enum process_state {
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,
    PROCESS_TERMINATED
};

// MINIX-inspired multi-level priority scheduler: processes live in one of
// NUM_SCHED_QUEUES ready queues. Lower queue numbers are strictly higher
// priority and are always preferred by the scheduler over higher-numbered
// (lower priority) queues, mirroring MINIX 3's banded
// kernel-task/system/user/idle priority layout. Within a single queue,
// processes are scheduled round-robin.
#define NUM_SCHED_QUEUES 16

#define PRIO_TASK   0                     // Highest priority: time-critical kernel tasks
#define PRIO_SYSTEM 4                     // System server processes
#define PRIO_USER   8                     // Normal user processes
#define PRIO_IDLE   (NUM_SCHED_QUEUES - 1) // Lowest priority: idle process

// Entry point signature for kernel-thread-style processes created via
// process_create().
typedef void (*process_entry_t)(void *arg);

typedef struct process {
    uint32_t pid;
    enum process_state state;
    registers_t registers; // Saved CPU state (reserved for future trap/user-mode based switching)

    uint64_t kernel_rsp;       // Saved kernel stack pointer, used by context_switch()
    uint8_t *kernel_stack;     // Base of this process's allocated kernel stack (for freeing)
    uint64_t kernel_stack_top; // Top of the kernel stack for this process

    process_entry_t entry; // Entry point run by process_trampoline() on first switch-in
    void *arg;              // Argument passed to entry

    uint8_t priority;    // Scheduling queue: 0 = highest, NUM_SCHED_QUEUES-1 = lowest
    uint32_t quantum;    // Timer ticks granted per scheduling turn
    uint32_t ticks_left; // Ticks remaining in the current turn

    // Ring-3 support: when is_user is set, process_trampoline() drops to
    // CPL=3 at user_entry/user_stack_top via enter_user_mode() instead of
    // calling `entry` directly. Currently these processes share the
    // kernel's single address space (see process_create_user() in
    // scheduler.c) -- per-process address space isolation is deferred to
    // the fork-exec work.
    uint8_t is_user;
    uint64_t user_entry;
    uint64_t user_stack_top;

    struct process* next; // Next process in its ready queue (or NULL)
    struct process* prev; // Previous process in its ready queue (or NULL)
} process_t;

// Implemented in src/boot/enter_user_mode.asm. Performs the one-time
// ring0->ring3 IRETQ transition for a brand new user process's first run.
// Does not return.
extern void enter_user_mode(uint64_t entry, uint64_t user_stack);

#endif // PROCESS_H
