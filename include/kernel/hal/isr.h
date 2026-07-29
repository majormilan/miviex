#ifndef ISR_H
#define ISR_H

#include <kernel/types.h>
#include <stdint.h>

// Structure for interrupt registers. Field order here must exactly match
// isr_common_stub's/syscall_stub's push order (src/boot/isr.asm), read in
// reverse (last pushed = lowest address = first field, since `regs` is set
// to RSP right after all the pushes): the stub pushes
// rax,rbx,rcx,rdx,rsi,rdi,rbp,r8..r15, so from low to high address that's
// r15..r8, rbp, rdi, rsi, rdx, rcx, rbx, rax.
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

// ISR handler function type
typedef void (*isr_t)(registers_t*);

// Function to register an interrupt handler
void register_interrupt_handler(uint8_t n, isr_t handler);
void isr_timer_handler(registers_t *regs);

#endif // ISR_H