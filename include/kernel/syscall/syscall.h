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
#define SYS_rt_sigaction    13
#define SYS_rt_sigprocmask  14
#define SYS_rt_sigreturn    15
#define SYS_ioctl           16
#define SYS_readv           19
#define SYS_pipe            22
#define SYS_writev          20
#define SYS_access          21
#define SYS_dup             32
#define SYS_dup2            33
#define SYS_getpid          39
#define SYS_fork            57
#define SYS_vfork           58
#define SYS_execve          59
#define SYS_exit            60
#define SYS_wait4           61
#define SYS_kill            62
#define SYS_fcntl           72
#define SYS_fsync           74
#define SYS_fdatasync       75
#define SYS_getdents        78
#define SYS_getcwd          79
#define SYS_chdir           80
#define SYS_fchdir          81
#define SYS_getuid          102
#define SYS_getgid          104
#define SYS_geteuid         107
#define SYS_getegid         108
#define SYS_setpgid         109
#define SYS_getppid         110
#define SYS_getpgrp         111
#define SYS_setsid          112
#define SYS_getpgid         121
#define SYS_getsid          124
#define SYS_tkill           200
#define SYS_arch_prctl      158
#define SYS_getdents64      217
#define SYS_set_tid_address 218
#define SYS_exit_group      231
#define SYS_tgkill          234
#define SYS_openat          257
#define SYS_newfstatat      262
#define SYS_faccessat       269
#define SYS_dup3            292
#define SYS_pipe2           293

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
SYSCALL(sys_fcntl);
SYSCALL(sys_getdents64);
SYSCALL(sys_getdents);
SYSCALL(sys_pipe);
SYSCALL(sys_pipe2);

// Process syscalls (src/kernel/syscall/proc.c).
SYSCALL(sys_fork);
SYSCALL(sys_execve);
SYSCALL(sys_wait4);
SYSCALL(sys_getppid);
SYSCALL(sys_chdir);
SYSCALL(sys_fchdir);
SYSCALL(sys_getcwd);

// Signals, process groups and sessions (src/kernel/proc/signal.c).
SYSCALL(sys_rt_sigaction);
SYSCALL(sys_rt_sigprocmask);
SYSCALL(sys_rt_sigreturn);
SYSCALL(sys_kill);
SYSCALL(sys_tkill);
SYSCALL(sys_tgkill);
SYSCALL(sys_setpgid);
SYSCALL(sys_getpgid);
SYSCALL(sys_getpgrp);
SYSCALL(sys_setsid);
SYSCALL(sys_getsid);

// Copy a NUL-terminated user string (at most max bytes incl. NUL),
// checking every page it touches. 0, -EFAULT or -ENAMETOOLONG.
long copy_user_str(char *dst, long src, size_t max);

// Current directory of the calling process.
struct inode *cwd_inode(void);

int init_syscalls(void);
void syscall_dispatch(registers_t *regs);

#endif /* SYSCALL_H */
