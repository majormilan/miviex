#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <kernel/proc/process.h>

// Default kernel-stack size allocated for each process created by
// process_create().
#define PROCESS_KERNEL_STACK_SIZE (16 * 1024)

// Initializes the scheduler's ready queues. Must be called once before any
// process_create()/scheduler_start() calls.
void scheduler_init(void);

// Creates a new kernel-thread-style process: `entry(arg)` starts running on
// its own freshly allocated kernel stack the first time it is switched in,
// at the given MINIX-style priority (0 = highest ... NUM_SCHED_QUEUES-1 =
// lowest; see PRIO_* in process.h). The process is placed at the back of
// its priority's ready queue. Returns NULL on allocation failure.
process_t* process_create(process_entry_t entry, void *arg, uint8_t priority);

// Create a ring-3 process that starts at `entry` with `rsp` in address
// space `as` (ownership passes to the process).
process_t* process_create_user(address_space_t *as, uint64_t entry, uint64_t rsp, uint8_t priority);

// Hands off control to the scheduler for the first time, switching from the
// kernel's boot-time call stack into the highest-priority runnable process.
// Does not return.
void scheduler_start(void);

// Called from the timer interrupt handler on every tick. Ages the currently
// running process's remaining quantum and triggers a reschedule once it
// reaches zero.
void scheduler_tick(void);

// Voluntarily gives up the remainder of the current quantum and reschedules
// immediately.
void scheduler_yield(void);

// Returns the process currently running on the CPU (or NULL before
// scheduler_start() has been called).
process_t* scheduler_current(void);

// Terminate the current process and switch away for good.
void scheduler_exit(void) __attribute__((noreturn));

#endif // SCHEDULER_H
