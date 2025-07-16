#include <kernel/syscall/syscall.h>
#include <kernel/hal/idt.h>
#include <kernel/hal/io.h>
#include <kernel/video/vga.h>

void *syscalls[MAX_SYSCALLS] = {
    [0] = &puts,
};

void syscall_handler(registers_t *regs) {
    if (regs->rax >= MAX_SYSCALLS) {
        return;
    }

    void *location = syscalls[regs->rax];

    int ret;
    asm volatile (" \
        push %1; \
        push %2; \
        push %3; \
        push %4; \
        push %5; \
        call *%6; \
        pop %%rbx; \
        pop %%rbx; \
        pop %%rbx; \
        pop %%rbx; \
        pop %%rbx; \
    " : "=a" (ret) : "r" (regs->rdi), "r" (regs->rsi), "r" (regs->rdx), "r" (regs->rcx), "r" (regs->rbx), "r" (location));

    regs->rax = ret;
}

void init_syscalls() {
    register_interrupt_handler(128, syscall_handler);
}