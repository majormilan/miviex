#include <kernel/hal/isr.h>
#include <kernel/video/vga.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/proc/scheduler.h>
#include <kernel/types.h>
#include <kernel/log.h>

static const char *exception_messages[32] = {
    "Division Error",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

// Array of interrupt handler functions
isr_t interrupt_handlers[256];

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
}

static uint64_t read_cr2(void) {
    uint64_t v;
    asm volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

static void dump(registers_t *r) {
    klog(LOG_FAIL, "trap", "rip=%lx cs=%lx rflags=%lx rsp=%lx ss=%lx",
         r->rip, r->cs, r->rflags, r->rsp, r->ss);
    klog(LOG_FAIL, "trap", "rax=%lx rbx=%lx rcx=%lx rdx=%lx",
         r->rax, r->rbx, r->rcx, r->rdx);
    klog(LOG_FAIL, "trap", "rsi=%lx rdi=%lx rbp=%lx r8=%lx",
         r->rsi, r->rdi, r->rbp, r->r8);
    klog(LOG_FAIL, "trap", "r9=%lx r10=%lx r11=%lx r12=%lx",
         r->r9, r->r10, r->r11, r->r12);
    klog(LOG_FAIL, "trap", "r13=%lx r14=%lx r15=%lx", r->r13, r->r14, r->r15);
}

/* Linux signal a CPU exception turns into for a user process. */
static int fault_signal(uint64_t vec) {
    switch (vec) {
    case 0: case 16: case 19: return 8; /* SIGFPE */
    case 3: return 5;                   /* SIGTRAP */
    case 6: return 4;                   /* SIGILL */
    case 17: return 7;                  /* SIGBUS */
    }
    return 11;                          /* SIGSEGV */
}

/*
 * Unhandled vector. A fault raised in ring 3 kills only the offending
 * process; anything else is a kernel bug and halts the machine.
 */
static void fault(registers_t *r) {
    const char *name = r->int_no < 32 ? exception_messages[r->int_no] : "Unexpected interrupt";
    bool user = (r->cs & 3) == 3;
    process_t *p = scheduler_current();

    if (r->int_no == 14) {
        /* err: bit0 present, bit1 write, bit2 user, bit4 instruction fetch */
        klog(LOG_FAIL, "trap", "%s at %lx (%s %s%s) rip=%lx",
             name, read_cr2(),
             r->err_code & 1 ? "protection" : "not-present",
             r->err_code & 2 ? "write" : "read",
             r->err_code & 16 ? " exec" : "", r->rip);
    } else {
        klog(LOG_FAIL, "trap", "%s (vector %lu, err %lx) rip=%lx",
             name, r->int_no, r->err_code, r->rip);
    }

    if (user && p != NULL) {
        klog(LOG_FAIL, "trap", "Killing pid=%d", p->pid);
        scheduler_exit(fault_signal(r->int_no)); /* "killed by signal" */
    }

    dump(r);
    klog(LOG_FAIL, "trap", "Kernel panic, system halted");
    for (;;) {
        asm volatile("cli; hlt");
    }
}

void isr_handler(registers_t *regs) {
    // Acknowledge IRQs *before* dispatching to their handler. Some handlers
    // (namely the timer, which drives scheduler_tick()) may context-switch
    // away here and not "return" until much later; if we waited until after
    // the handler returned to send EOI, the PIC would withhold further
    // same/lower-priority IRQs (effectively hanging IRQ0, and therefore
    // everything else) for however long that takes.
    bool is_irq = (regs->int_no >= 32 && regs->int_no <= 47);
    if (is_irq) {
        if (regs->int_no >= 40) { // Slave PIC
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20); // Master PIC
    }

    // Check if we have a custom handler for this interrupt
    if (interrupt_handlers[regs->int_no] != 0) {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    } else if (!is_irq) {
        fault(regs); /* unclaimed IRQs (e.g. spurious IRQ7) are dropped */
    }
}

void isr_timer_handler(registers_t *regs) {
    (void)regs;
    // EOI was already sent by isr_handler() before dispatching here. Simply
    // advance the scheduler's notion of time; scheduler_tick() will
    // context-switch away if the running process's quantum just expired.
    scheduler_tick();
}