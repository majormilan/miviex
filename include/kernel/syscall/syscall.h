#ifndef SYSCALL_H
#define SYSCALL_H

#include <kernel/hal/isr.h>

/* Linux x86-64 syscall numbers. */
#define SYS_write      1
#define SYS_exit       60
#define SYS_exit_group 231

int init_syscalls(void);
void syscall_dispatch(registers_t *regs);

#endif /* SYSCALL_H */
