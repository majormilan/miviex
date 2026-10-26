#include <kernel/proc/scheduler.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/hal/gdt.h>
#include <kernel/hal/io.h>
#include <kernel/libc/string.h>
#include <kernel/log.h>
#include <stddef.h>

// Implemented in src/boot/context_switch.asm. Saves the callee-saved
// registers and RFLAGS of the currently running context onto its own stack,
// records the resulting stack pointer at *old_rsp, switches RSP to
// new_rsp, and restores the callee-saved registers/RFLAGS that were saved
// there (either by a previous context_switch(), or synthesized by
// process_create() for a brand new process) before returning.
extern void context_switch(uint64_t *old_rsp, uint64_t new_rsp);

// MINIX gives more time-critical (lower-numbered) queues shorter, more
// frequent turns, and less urgent (higher-numbered) queues longer
// uninterrupted bursts once they finally get the CPU.
static uint32_t default_quantum_for_priority(uint8_t priority) {
    return 2 + priority;
}

static process_t *ready_head[NUM_SCHED_QUEUES];
static process_t *ready_tail[NUM_SCHED_QUEUES];
static process_t *current = NULL;
static process_t *all_procs = NULL;
static uint32_t next_pid = 0; // the idle process is created first: pid 0

// Scratch slot used to receive the kernel's own boot-time stack pointer the
// first time scheduler_start() switches away from it. It is never
// meaningfully read back since the kernel never resumes raw boot-time
// execution once processes take over.
static uint64_t bootstrap_rsp;

static void queue_push_back(process_t *proc) {
    proc->next = NULL;
    proc->prev = ready_tail[proc->priority];
    if (ready_tail[proc->priority] != NULL) {
        ready_tail[proc->priority]->next = proc;
    } else {
        ready_head[proc->priority] = proc;
    }
    ready_tail[proc->priority] = proc;
}

static process_t *queue_pop_front(uint8_t priority) {
    process_t *proc = ready_head[priority];
    if (proc == NULL) {
        return NULL;
    }

    ready_head[priority] = proc->next;
    if (ready_head[priority] != NULL) {
        ready_head[priority]->prev = NULL;
    } else {
        ready_tail[priority] = NULL;
    }

    proc->next = NULL;
    proc->prev = NULL;
    return proc;
}

// Strict priority scan: always prefer the lowest-numbered non-empty queue,
// then round-robin within it (queue_push_back()/queue_pop_front() keep each
// queue itself FIFO).
static process_t *pick_next(void) {
    for (int priority = 0; priority < NUM_SCHED_QUEUES; priority++) {
        if (ready_head[priority] != NULL) {
            return queue_pop_front((uint8_t)priority);
        }
    }
    return NULL;
}

void scheduler_init(void) {
    for (int i = 0; i < NUM_SCHED_QUEUES; i++) {
        ready_head[i] = NULL;
        ready_tail[i] = NULL;
    }
    current = NULL;
    next_pid = 0;
    klog(LOG_OK, "sched", "MINIX-style priority scheduler initialized (%d queues)", NUM_SCHED_QUEUES);
}

// Every brand new process starts here (via context_switch()'s `ret`), on
// its own kernel stack, with interrupts enabled. Reads its entry point/arg
// out of the now-current process_t and calls it.
static void process_trampoline(void) {
    process_t *self = current;

    if (self != NULL && self->is_user) {
        // Does not return: drops straight to ring-3. Any later re-entry to
        // ring0 for this process happens via isr_common_stub's own iretq
        // (interrupt/syscall return), not through here again.
        enter_user_mode(self->user_entry, self->user_stack_top);
    } else if (self != NULL && self->entry != NULL) {
        self->entry(self->arg);
    }

    scheduler_exit(0);
}

// Allocate a process with a kernel stack holding the frame context_switch()
// expects: the asm pops r15, r14, r13, r12, rbx, rbp, rflags and then
// `ret`s, so the first switch-in lands in process_trampoline().
static process_t *process_alloc(uint8_t priority) {
    if (priority >= NUM_SCHED_QUEUES) {
        priority = NUM_SCHED_QUEUES - 1;
    }

    process_t *proc = (process_t*)k_malloc(sizeof(process_t));
    if (proc == NULL) {
        klog(LOG_FAIL, "sched", "Failed to allocate process_t");
        return NULL;
    }
    memset(proc, 0, sizeof(process_t));

    uint8_t *stack = (uint8_t*)k_malloc(PROCESS_KERNEL_STACK_SIZE);
    if (stack == NULL) {
        klog(LOG_FAIL, "sched", "Failed to allocate kernel stack for new process");
        k_free(proc);
        return NULL;
    }

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->all_next = all_procs;
    all_procs = proc;
    proc->priority = priority;
    proc->quantum = default_quantum_for_priority(priority);
    proc->ticks_left = proc->quantum;
    proc->kernel_stack = stack;
    proc->kernel_stack_top = (uint64_t)(stack + PROCESS_KERNEL_STACK_SIZE);

    // Default x87/SSE state, as after fninit + ldmxcsr 0x1f80.
    *(uint16_t *)&proc->fpu[0] = 0x37F;   // FCW
    *(uint32_t *)&proc->fpu[24] = 0x1F80; // MXCSR

    uint64_t *sp = (uint64_t*)proc->kernel_stack_top;
    *(--sp) = (uint64_t)process_trampoline; // return address, popped last by `ret`
    *(--sp) = 0x202; // rflags: IF=1 (interrupts enabled), bit 1 reserved-set
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15 - top of stack, popped first
    proc->kernel_rsp = (uint64_t)sp;

    return proc;
}

process_t* process_create(process_entry_t entry, void *arg, uint8_t priority) {
    process_t *proc = process_alloc(priority);
    if (proc == NULL) {
        return NULL;
    }
    proc->entry = entry;
    proc->arg = arg;

    queue_push_back(proc);
    klog(LOG_OK, "sched", "Created process pid=%d priority=%d quantum=%d ticks", proc->pid, proc->priority, proc->quantum);
    return proc;
}

process_t* process_create_user(address_space_t *as, uint64_t entry, uint64_t rsp, uint8_t priority) {
    process_t *proc = process_alloc(priority);
    if (proc == NULL) {
        return NULL;
    }
    proc->is_user = 1;
    proc->as = as;
    proc->user_entry = entry;
    proc->user_stack_top = rsp;

    queue_push_back(proc);
    klog(LOG_OK, "sched", "Created user process pid=%d priority=%d entry=0x%lx stack=0x%lx",
         proc->pid, proc->priority, proc->user_entry, proc->user_stack_top);
    return proc;
}

#define MSR_FS_BASE 0xC0000100

// Hand the CPU from prev to next: user state the kernel never touches
// (FPU/SSE, TLS base, address space) is swapped here, then the stacks.
static void switch_to(process_t *prev, process_t *next) {
    if (prev != NULL && prev->is_user) {
        asm volatile("fxsave (%0)" : : "r"(prev->fpu) : "memory");
        prev->fs_base = rdmsr(MSR_FS_BASE);
    }
    if (next->is_user) {
        asm volatile("fxrstor (%0)" : : "r"(next->fpu) : "memory");
        wrmsr(MSR_FS_BASE, next->fs_base);
        vmm_switch_address_space(next->as);
    }

    // The CPU loads SS:RSP from TSS.RSP0 on any ring3->ring0 transition
    // (interrupt/syscall), so this must always point at whichever
    // process's kernel stack is about to become "current".
    tss_set_stack(next->kernel_stack_top);
    context_switch(prev != NULL ? &prev->kernel_rsp : &bootstrap_rsp, next->kernel_rsp);
}

void scheduler_start(void) {
    process_t *next = pick_next();
    if (next == NULL) {
        klog(LOG_FAIL, "sched", "scheduler_start() called with no runnable processes!");
        return;
    }

    next->state = PROCESS_RUNNING;
    current = next;
    klog(LOG_INFO, "sched", "Starting scheduler with pid=%d (priority=%d)", next->pid, next->priority);
    switch_to(NULL, next);
    // Never actually reached: nothing ever switches back into the discarded
    // boot-time stack.
}

void scheduler_yield(void) {
    process_t *prev = current;
    process_t *next = pick_next();

    if (next == NULL) {
        // Nothing else is runnable right now; keep running whatever we
        // have.
        return;
    }

    if (prev != NULL && prev->state == PROCESS_RUNNING) {
        prev->state = PROCESS_READY;
        prev->ticks_left = prev->quantum;
        queue_push_back(prev);
    }

    next->state = PROCESS_RUNNING;
    current = next;

    if (prev == next) {
        return; // Nothing to actually switch.
    }

    switch_to(prev, next);
}

void scheduler_tick(void) {
    if (current == NULL) {
        return;
    }

    if (current->ticks_left > 0) {
        current->ticks_left--;
    }

    if (current->ticks_left == 0) {
        scheduler_yield();
    }
}

void sleep_on(void *chan) {
    current->wait_chan = chan;
    current->state = PROCESS_BLOCKED;
    scheduler_yield();
}

void wakeup(void *chan) {
    for (process_t *p = all_procs; p != NULL; p = p->all_next) {
        if (p->state == PROCESS_BLOCKED && p->wait_chan == chan) {
            p->wait_chan = NULL;
            p->state = PROCESS_READY;
            queue_push_back(p);
        }
    }
}

process_t *process_find(uint32_t pid) {
    for (process_t *p = all_procs; p != NULL; p = p->all_next) {
        if (p->pid == pid) {
            return p;
        }
    }
    return NULL;
}

process_t *process_list(void) {
    return all_procs;
}

void process_reap(process_t *p) {
    for (process_t **pp = &all_procs; *pp != NULL; pp = &(*pp)->all_next) {
        if (*pp == p) {
            *pp = p->all_next;
            break;
        }
    }
    k_free(p->kernel_stack);
    k_free(p);
}

void scheduler_exit(int status) {
    process_t *init = process_find(1);

    if (current->pid == 1) {
        klog(LOG_FAIL, "sched", "init exited (status 0x%x), kernel panic", status);
        for (;;) {
            asm volatile("cli; hlt");
        }
    }

    for (int i = 0; i < MAX_FDS; i++) {
        file_put(current->fds[i]);
        current->fds[i] = NULL;
    }
    if (current->as != NULL) {
        // The kernel half is shared, so we keep running on the kernel AS
        // while the user half is torn down.
        vmm_switch_address_space(vmm_get_kernel_address_space());
        vmm_destroy_address_space(current->as);
        current->as = NULL;
        current->is_user = 0;
    }

    // Orphans are adopted by init, which reaps them.
    for (process_t *p = all_procs; p != NULL; p = p->all_next) {
        if (p->ppid == current->pid && p != current && init != NULL) {
            p->ppid = 1;
            if (p->state == PROCESS_TERMINATED) {
                wakeup(init);
            }
        }
    }

    current->exit_status = status;
    current->state = PROCESS_TERMINATED;
    wakeup(process_find(current->ppid));

    // Never re-queued: scheduler_yield() only re-queues running processes.
    // Loop in case nothing else is runnable yet.
    while (1) {
        scheduler_yield();
    }
}

extern void syscall_return(void);

process_t *process_fork(void) {
    process_t *parent = current;
    process_t *child = process_alloc(parent->priority);
    if (child == NULL) {
        return NULL;
    }

    child->as = vmm_clone(parent->as);
    if (child->as == NULL) {
        process_reap(child);
        return NULL;
    }
    child->is_user = 1;
    child->ppid = parent->pid;
    child->fs_base = parent->fs_base;
    child->cwd = parent->cwd;
    asm volatile("fxsave (%0)" : : "r"(child->fpu) : "memory");
    for (int i = 0; i < MAX_FDS; i++) {
        if (parent->fds[i] != NULL) {
            child->fds[i] = file_get(parent->fds[i]);
            child->cloexec[i] = parent->cloexec[i];
        }
    }

    // Same user frame, but fork() returns 0 in the child. The first switch
    // into the child "returns" to syscall_return (src/boot/syscall.asm),
    // which pops this frame and sysrets to user mode.
    registers_t *f = user_frame(child);
    *f = *user_frame(parent);
    f->rax = 0;
    uint64_t *sp = (uint64_t *)f;
    *(--sp) = (uint64_t)syscall_return;
    *(--sp) = 0x002; // rflags: IF=0, as on the syscall path
    for (int i = 0; i < 6; i++) {
        *(--sp) = 0; // rbp, rbx, r12-r15
    }
    child->kernel_rsp = (uint64_t)sp;

    queue_push_back(child);
    return child;
}

process_t* scheduler_current(void) {
    return current;
}
