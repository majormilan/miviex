#ifndef ISR_H
#define ISR_H

#include <kernel/types.h>

// Structure for interrupt registers
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

// ISR handler function type
typedef void (*isr_t)(registers_t*);

// Function to register an interrupt handler
void register_interrupt_handler(uint8_t n, isr_t handler);
void isr_timer_handler(registers_t *regs);

#endif // ISR_H