#ifndef SYSCALL_H
#define SYSCALL_H

#include <kernel/hal/isr.h>

/* Linux x86-64 syscall numbers. */
#define SYS_write           1
#define SYS_mmap            9
#define SYS_mprotect        10
#define SYS_munmap          11
#define SYS_brk             12
#define SYS_ioctl           16
#define SYS_writev          20
#define SYS_getpid          39
#define SYS_exit            60
#define SYS_arch_prctl      158
#define SYS_set_tid_address 218
#define SYS_exit_group      231

// Memory syscalls (src/kernel/syscall/mman.c).
long sys_brk(long addr, long a2, long a3, long a4, long a5, long a6);
long sys_mmap(long addr, long len, long prot, long flags, long fd, long off);
long sys_munmap(long addr, long len, long a3, long a4, long a5, long a6);
long sys_mprotect(long addr, long len, long prot, long a4, long a5, long a6);

int init_syscalls(void);
void syscall_dispatch(registers_t *regs);

#endif /* SYSCALL_H */
