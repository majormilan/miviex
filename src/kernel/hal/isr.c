#include <kernel/hal/isr.h>
#include <kernel/video/vga.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/proc/scheduler.h>
#include <kernel/types.h>

const char *exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Unknown Interrupt",
    "Coprocessor Fault",
    "Alignment Check",
    "Machine Check",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved"
};

// Array of interrupt handler functions
isr_t interrupt_handlers[256];

void register_interrupt_handler(uint8_t n, isr_t handler) {
    interrupt_handlers[n] = handler;
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
    } else {
        terminal_print("Recieved Interrupt: ");
        terminal_print_num(regs->int_no);
        terminal_print("\n");
        if (regs->int_no < 32) { // CPU exceptions
            terminal_print(exception_messages[regs->int_no]);
            terminal_print("\n");
        }

        terminal_print("RAX: "); terminal_print_num(regs->rax); terminal_print("\n");
        terminal_print("RBX: "); terminal_print_num(regs->rbx); terminal_print("\n");
        terminal_print("RCX: "); terminal_print_num(regs->rcx); terminal_print("\n");
        terminal_print("RDX: "); terminal_print_num(regs->rdx); terminal_print("\n");
        terminal_print("RSI: "); terminal_print_num(regs->rsi); terminal_print("\n");
        terminal_print("RDI: "); terminal_print_num(regs->rdi); terminal_print("\n");
        terminal_print("RBP: "); terminal_print_num(regs->rbp); terminal_print("\n");
        terminal_print("RSP: "); terminal_print_num(regs->rsp); terminal_print("\n");
        terminal_print("RIP: "); terminal_print_num(regs->rip); terminal_print("\n");
        terminal_print("CS: "); terminal_print_num(regs->cs); terminal_print("\n");
        terminal_print("RFLAGS: "); terminal_print_num(regs->rflags); terminal_print("\n");
        terminal_print("Error Code: "); terminal_print_num(regs->err_code); terminal_print("\n");

        terminal_print("SYSTEM HALTED!");
        while(1);
    }
}

void isr_timer_handler(registers_t *regs) {
    (void)regs;
    // EOI was already sent by isr_handler() before dispatching here. Simply
    // advance the scheduler's notion of time; scheduler_tick() will
    // context-switch away if the running process's quantum just expired.
    scheduler_tick();
}