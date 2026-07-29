#include <kernel/proc/scheduler.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/hal/gdt.h>
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
static uint32_t next_pid = 1;

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
    next_pid = 1;
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

    if (self != NULL) {
        self->state = PROCESS_TERMINATED;
        klog(LOG_INFO, "sched", "Process pid=%d exited", self->pid);
    }

    // A kernel-thread's entry function returned: there is nothing to return
    // to, so just yield forever (this process will never be re-queued
    // since scheduler_yield() only re-queues non-terminated processes).
    while (1) {
        scheduler_yield();
    }
}

process_t* process_create(process_entry_t entry, void *arg, uint8_t priority) {
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
    proc->entry = entry;
    proc->arg = arg;
    proc->priority = priority;
    proc->quantum = default_quantum_for_priority(priority);
    proc->ticks_left = proc->quantum;
    proc->kernel_stack = stack;
    proc->kernel_stack_top = (uint64_t)(stack + PROCESS_KERNEL_STACK_SIZE);

    // Synthesize the stack frame context_switch() expects to find. The asm
    // pushes, in order: rflags, rbp, rbx, r12, r13, r14, r15 (so r15 ends up
    // on top/lowest address), and pops in the reverse order: r15, r14, r13,
    // r12, rbx, rbp, rflags, then `ret`. We build that same layout here,
    // top (lowest address, final kernel_rsp) to bottom, with
    // process_trampoline as the "return address" so the first switch-in
    // jumps straight there.
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

    queue_push_back(proc);
    klog(LOG_OK, "sched", "Created process pid=%d priority=%d quantum=%d ticks", proc->pid, proc->priority, proc->quantum);
    return proc;
}

process_t* process_create_user(const uint8_t *code, size_t code_len, uint8_t priority) {
    if (priority >= NUM_SCHED_QUEUES) {
        priority = NUM_SCHED_QUEUES - 1;
    }
    if (code_len > PAGE_SIZE) {
        klog(LOG_FAIL, "sched", "process_create_user: code_len %d exceeds one page", (int)code_len);
        return NULL;
    }

    process_t *proc = (process_t*)k_malloc(sizeof(process_t));
    if (proc == NULL) {
        klog(LOG_FAIL, "sched", "Failed to allocate process_t");
        return NULL;
    }
    memset(proc, 0, sizeof(process_t));

    uint8_t *kstack = (uint8_t*)k_malloc(PROCESS_KERNEL_STACK_SIZE);
    uint8_t *code_page = (uint8_t*)pmm_alloc_page();
    uint8_t *user_stack_page = (uint8_t*)pmm_alloc_page();
    if (kstack == NULL || code_page == NULL || user_stack_page == NULL) {
        klog(LOG_FAIL, "sched", "Failed to allocate resources for new user process");
        if (kstack) k_free(kstack);
        if (code_page) pmm_free_page(code_page);
        if (user_stack_page) pmm_free_page(user_stack_page);
        k_free(proc);
        return NULL;
    }

    // The kernel identity-maps physical == virtual for addresses below 1GB
    // (see src/boot/main.asm), so pmm_alloc_page()'s returned physical
    // address can be written to directly here.
    memset(code_page, 0, PAGE_SIZE);
    memcpy(code_page, code, code_len);

    address_space_t *kas = vmm_get_kernel_address_space();
    uint64_t code_virt = (uint64_t)code_page;
    uint64_t stack_virt = (uint64_t)user_stack_page;

    // Code page: user-accessible + present, but not writable (read/exec
    // only). Stack page: user-accessible + writable, not executable
    // (no NX support wired up yet, but we don't need exec here anyway).
    vmm_map_page(kas, code_virt, (uint64_t)code_page, PAGE_PRESENT | PAGE_USER);
    vmm_map_page(kas, stack_virt, (uint64_t)user_stack_page, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

    proc->pid = next_pid++;
    proc->state = PROCESS_READY;
    proc->is_user = 1;
    proc->user_entry = code_virt;
    proc->user_stack_top = stack_virt + PAGE_SIZE;
    proc->priority = priority;
    proc->quantum = default_quantum_for_priority(priority);
    proc->ticks_left = proc->quantum;
    proc->kernel_stack = kstack;
    proc->kernel_stack_top = (uint64_t)(kstack + PROCESS_KERNEL_STACK_SIZE);

    // Same synthesized context_switch() stack frame as process_create():
    // process_trampoline is the first thing this process runs on its first
    // switch-in.
    uint64_t *sp = (uint64_t*)proc->kernel_stack_top;
    *(--sp) = (uint64_t)process_trampoline;
    *(--sp) = 0x202; // rflags: IF=1
    *(--sp) = 0; // rbp
    *(--sp) = 0; // rbx
    *(--sp) = 0; // r12
    *(--sp) = 0; // r13
    *(--sp) = 0; // r14
    *(--sp) = 0; // r15

    proc->kernel_rsp = (uint64_t)sp;

    queue_push_back(proc);
    klog(LOG_OK, "sched", "Created user process pid=%d priority=%d entry=0x%x stack=0x%x",
         proc->pid, proc->priority, proc->user_entry, proc->user_stack_top);
    return proc;
}

void scheduler_start(void) {
    process_t *next = pick_next();
    if (next == NULL) {
        klog(LOG_FAIL, "sched", "scheduler_start() called with no runnable processes!");
        return;
    }

    next->state = PROCESS_RUNNING;
    current = next;
    tss_set_stack(next->kernel_stack_top);
    klog(LOG_INFO, "sched", "Starting scheduler with pid=%d (priority=%d)", next->pid, next->priority);
    context_switch(&bootstrap_rsp, next->kernel_rsp);
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

    if (prev != NULL && prev->state != PROCESS_TERMINATED) {
        prev->state = PROCESS_READY;
        prev->ticks_left = prev->quantum;
        queue_push_back(prev);
    }

    next->state = PROCESS_RUNNING;
    current = next;

    if (prev == next) {
        return; // Nothing to actually switch.
    }

    // The CPU loads SS:RSP from TSS.RSP0 on any ring3->ring0 transition
    // (interrupt/syscall), so this must always point at whichever
    // process's kernel stack is about to become "current".
    tss_set_stack(next->kernel_stack_top);

    context_switch(prev != NULL ? &prev->kernel_rsp : &bootstrap_rsp, next->kernel_rsp);
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

process_t* scheduler_current(void) {
    return current;
}
