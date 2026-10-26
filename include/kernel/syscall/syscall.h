#ifndef SYSCALL_H
#define SYSCALL_H

#include <kernel/hal/isr.h>

/* Linux x86-64 syscall numbers. */
#define SYS_read            0
#define SYS_write           1
#define SYS_open            2
#define SYS_close           3
#define SYS_stat            4
#define SYS_fstat           5
#define SYS_lstat           6
#define SYS_lseek           8
#define SYS_mmap            9
#define SYS_mprotect        10
#define SYS_munmap          11
#define SYS_brk             12
#define SYS_ioctl           16
#define SYS_readv           19
#define SYS_writev          20
#define SYS_access          21
#define SYS_dup             32
#define SYS_dup2            33
#define SYS_getpid          39
#define SYS_exit            60
#define SYS_arch_prctl      158
#define SYS_set_tid_address 218
#define SYS_exit_group      231
#define SYS_openat          257
#define SYS_newfstatat      262
#define SYS_faccessat       269
#define SYS_dup3            292

// Memory syscalls (src/kernel/syscall/mman.c).
long sys_brk(long addr, long a2, long a3, long a4, long a5, long a6);
long sys_mmap(long addr, long len, long prot, long flags, long fd, long off);
long sys_munmap(long addr, long len, long a3, long a4, long a5, long a6);
long sys_mprotect(long addr, long len, long prot, long a4, long a5, long a6);

// File syscalls (src/kernel/syscall/fs.c).
#define SYSCALL(name) long name(long, long, long, long, long, long)
SYSCALL(sys_read);
SYSCALL(sys_write);
SYSCALL(sys_open);
SYSCALL(sys_openat);
SYSCALL(sys_close);
SYSCALL(sys_stat);
SYSCALL(sys_fstat);
SYSCALL(sys_newfstatat);
SYSCALL(sys_lseek);
SYSCALL(sys_ioctl);
SYSCALL(sys_readv);
SYSCALL(sys_writev);
SYSCALL(sys_access);
SYSCALL(sys_faccessat);
SYSCALL(sys_dup);
SYSCALL(sys_dup2);
SYSCALL(sys_dup3);

int init_syscalls(void);
void syscall_dispatch(registers_t *regs);

#endif /* SYSCALL_H */
