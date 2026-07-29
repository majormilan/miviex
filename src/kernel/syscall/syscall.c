#include <kernel/syscall/syscall.h>
#include <kernel/hal/idt.h>
#include <kernel/hal/io.h>
#include <kernel/video/vga.h>

// Common syscall signature: up to 5 arguments, matching the x86-64 SysV
// integer argument registers (rdi, rsi, rdx, rcx, r8). Individual syscalls
// may ignore trailing arguments they don't need.
typedef long (*syscall_fn_t)(long, long, long, long, long);

void *syscalls[MAX_SYSCALLS] = {
    [0] = &puts,
};

void syscall_handler(registers_t *regs) {
    if (regs->rax >= MAX_SYSCALLS || syscalls[regs->rax] == NULL) {
        regs->rax = (uint64_t)-1;
        return;
    }

    // Let the C compiler place these arguments in the correct SysV
    // registers itself, rather than hand-rolling (and getting wrong) the
    // calling convention with inline asm.
    syscall_fn_t fn = (syscall_fn_t)syscalls[regs->rax];
    long ret = fn((long)regs->rdi, (long)regs->rsi, (long)regs->rdx,
                  (long)regs->rcx, (long)regs->r8);

    regs->rax = (uint64_t)ret;
}

int init_syscalls() {
    register_interrupt_handler(128, syscall_handler);
    return 0;
}