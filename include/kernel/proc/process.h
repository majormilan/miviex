#ifndef PROCESS_H
#define PROCESS_H

#include <stdint.h>
#include <stddef.h>

// Note: intentionally not including <stdbool.h> here — kernel/hal/isr.h (via
// kernel/types.h) already provides bool/true/false, and including both
// causes a `typedef _Bool bool;` redefinition conflict once stdbool.h's
// `bool` macro has already expanded.
#include <kernel/hal/isr.h>
#include <kernel/mm/vmm.h>
#include <kernel/vfs/vfs.h>
#include <kernel/proc/signal.h>

// Forward declaration for registers_t from isr.h
// typedef struct registers registers_t; // Removed this line

enum process_state {
    PROCESS_RUNNING,
    PROCESS_READY,
    PROCESS_BLOCKED,    // asleep on wait_chan
    PROCESS_TERMINATED  // zombie: exit_status kept until the parent reaps it
};

// MINIX-inspired multi-level priority scheduler: processes live in one of
// NUM_SCHED_QUEUES ready queues. Lower queue numbers are strictly higher
// priority and are always preferred by the scheduler over higher-numbered
// (lower priority) queues, mirroring MINIX 3's banded
// kernel-task/system/user/idle priority layout. Within a single queue,
// processes are scheduled round-robin.
#define NUM_SCHED_QUEUES 16
#define MAX_FDS 64

struct tty;

#define PRIO_TASK   0                     // Highest priority: time-critical kernel tasks
#define PRIO_SYSTEM 4                     // System server processes
#define PRIO_USER   8                     // Normal user processes
#define PRIO_IDLE   (NUM_SCHED_QUEUES - 1) // Lowest priority: idle process

// Entry point signature for kernel-thread-style processes created via
// process_create().
typedef void (*process_entry_t)(void *arg);

typedef struct process {
    uint32_t pid;
    uint32_t ppid;
    uint32_t uid, euid, suid; // real, effective, saved user id
    uint32_t gid, egid, sgid; // real, effective, saved group id
    uint32_t ngroups;
    uint32_t groups[32];      // supplementary groups (NGROUPS_MAX)
    uint32_t pgid;    // process group (job)
    uint32_t sid;     // session
    struct tty *ctty; // controlling terminal, NULL if none
    enum process_state state;
    int exit_status;  // wait() status word, valid once TERMINATED
    void *wait_chan;  // what a BLOCKED process sleeps on

    uint64_t kernel_rsp;       // Saved kernel stack pointer, used by context_switch()
    uint8_t *kernel_stack;     // Base of this process's allocated kernel stack (for freeing)
    uint64_t kernel_stack_top; // Top of the kernel stack for this process

    process_entry_t entry; // Entry point run by process_trampoline() on first switch-in
    void *arg;              // Argument passed to entry

    uint8_t priority;    // Scheduling queue: 0 = highest, NUM_SCHED_QUEUES-1 = lowest
    uint32_t quantum;    // Timer ticks granted per scheduling turn
    uint32_t ticks_left; // Ticks remaining in the current turn
    uint64_t cpu_ticks;  // Timer ticks spent running (CPU time)
    uint64_t child_ticks; // CPU time of reaped children (and theirs)
    uint64_t start_ticks; // jiffies at creation
    char comm[16];        // command name (basename of the executable)
    char *exe;            // path of the executable
    uint64_t arg_start, arg_end; // argv strings in the user stack

    // Ring-3 support: when is_user is set, process_trampoline() drops to
    // CPL=3 at user_entry/user_stack_top via enter_user_mode() instead of
    // calling `entry` directly.
    uint8_t is_user;
    uint64_t user_entry;
    uint64_t user_stack_top;
    address_space_t *as;    // User address space (NULL for kernel threads)
    uint64_t fs_base;       // TLS pointer (arch_prctl ARCH_SET_FS)
    file_t *fds[MAX_FDS];   // Open file descriptors
    uint8_t cloexec[MAX_FDS]; // FD_CLOEXEC per descriptor
    inode_t *cwd;           // Current directory (NULL = "/")
    uint32_t umask;         // File creation mask
    struct { uint64_t cur, max; } rlim[16]; // resource limits (RLIMIT_*)
    k_sigaction_t sigact[NSIG]; // Signal dispositions
    uint64_t sig_pending;   // bit n-1 set: signal n pending
    uint64_t sig_blocked;   // signal mask
    uint8_t fpu[512] __attribute__((aligned(16))); // fxsave area

    struct process* next; // Next process in its ready queue (or NULL)
    struct process* prev; // Previous process in its ready queue (or NULL)
    struct process* all_next; // Every process, for wait() and wakeup()
} process_t;

// Implemented in src/boot/enter_user_mode.asm. Performs the one-time
// ring0->ring3 IRETQ transition for a brand new user process's first run.
// Does not return.
extern void enter_user_mode(uint64_t entry, uint64_t user_stack);

#endif // PROCESS_H
