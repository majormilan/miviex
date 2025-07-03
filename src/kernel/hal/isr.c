#include <kernel/hal/isr.h>
#include <kernel/video/vga.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/keyboard.h>

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

    // If it's an IRQ, send EOI
    if (regs->int_no >= 32) {
        if (regs->int_no >= 40) { // Slave PIC
            outb(0xA0, 0x20);
        }
        outb(0x20, 0x20); // Master PIC
    }
}

void isr_timer_handler(registers_t *regs) {
    // For now, just send EOI
    if (regs->int_no >= 40) { // Slave PIC
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20); // Master PIC
}