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
#define SYS_poll            7
#define SYS_lseek           8
#define SYS_mmap            9
#define SYS_mprotect        10
#define SYS_munmap          11
#define SYS_brk             12
#define SYS_rt_sigaction    13
#define SYS_rt_sigprocmask  14
#define SYS_rt_sigreturn    15
#define SYS_ioctl           16
#define SYS_pread64         17
#define SYS_pwrite64        18
#define SYS_readv           19
#define SYS_pipe            22
#define SYS_writev          20
#define SYS_access          21
#define SYS_dup             32
#define SYS_dup2            33
#define SYS_pause           34
#define SYS_nanosleep       35
#define SYS_getpid          39
#define SYS_socket          41
#define SYS_fork            57
#define SYS_vfork           58
#define SYS_execve          59
#define SYS_exit            60
#define SYS_wait4           61
#define SYS_kill            62
#define SYS_uname           63
#define SYS_fcntl           72
#define SYS_fsync           74
#define SYS_fdatasync       75
#define SYS_truncate        76
#define SYS_ftruncate       77
#define SYS_getdents        78
#define SYS_getcwd          79
#define SYS_chdir           80
#define SYS_fchdir          81
#define SYS_rename          82
#define SYS_mkdir           83
#define SYS_rmdir           84
#define SYS_link            86
#define SYS_unlink          87
#define SYS_symlink         88
#define SYS_readlink        89
#define SYS_chmod           90
#define SYS_fchmod          91
#define SYS_chown           92
#define SYS_fchown          93
#define SYS_lchown          94
#define SYS_umask           95
#define SYS_gettimeofday    96
#define SYS_getrlimit       97
#define SYS_getrusage       98
#define SYS_sysinfo         99
#define SYS_times           100
#define SYS_getuid          102
#define SYS_syslog          103
#define SYS_getgid          104
#define SYS_setuid          105
#define SYS_setgid          106
#define SYS_geteuid         107
#define SYS_getegid         108
#define SYS_setpgid         109
#define SYS_getppid         110
#define SYS_getpgrp         111
#define SYS_getgroups       115
#define SYS_setgroups       116
#define SYS_setresuid       117
#define SYS_getresuid       118
#define SYS_setresgid       119
#define SYS_getresgid       120
#define SYS_setsid          112
#define SYS_setreuid        113
#define SYS_setregid        114
#define SYS_getpgid         121
#define SYS_setfsuid        122
#define SYS_setfsgid        123
#define SYS_getsid          124
#define SYS_rt_sigsuspend   130
#define SYS_statfs          137
#define SYS_fstatfs         138
#define SYS_vhangup         153
#define SYS_setrlimit       160
#define SYS_sync            162
#define SYS_mount           165
#define SYS_umount2         166
#define SYS_reboot          169
#define SYS_settimeofday    164
#define SYS_sethostname     170
#define SYS_setdomainname   171
#define SYS_tkill           200
#define SYS_time            201
#define SYS_arch_prctl      158
#define SYS_getdents64      217
#define SYS_set_tid_address 218
#define SYS_clock_settime   227
#define SYS_clock_gettime   228
#define SYS_clock_getres    229
#define SYS_clock_nanosleep 230
#define SYS_exit_group      231
#define SYS_tgkill          234
#define SYS_openat          257
#define SYS_mkdirat         258
#define SYS_fchownat        260
#define SYS_newfstatat      262
#define SYS_unlinkat        263
#define SYS_renameat        264
#define SYS_linkat          265
#define SYS_symlinkat       266
#define SYS_readlinkat      267
#define SYS_fchmodat        268
#define SYS_faccessat       269
#define SYS_ppoll           271
#define SYS_utimensat       280
#define SYS_dup3            292
#define SYS_pipe2           293
#define SYS_preadv          295
#define SYS_pwritev         296
#define SYS_prlimit64       302
#define SYS_renameat2       316
#define SYS_getrandom       318

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
SYSCALL(sys_lstat);
SYSCALL(sys_poll);
SYSCALL(sys_ppoll);
SYSCALL(sys_pread64);
SYSCALL(sys_pwrite64);
SYSCALL(sys_preadv);
SYSCALL(sys_pwritev);
SYSCALL(sys_mount);
SYSCALL(sys_umount2);
SYSCALL(sys_statfs);
SYSCALL(sys_fstatfs);
SYSCALL(sys_umask);
SYSCALL(sys_chmod);
SYSCALL(sys_fchmod);
SYSCALL(sys_fchmodat);
SYSCALL(sys_chown);
SYSCALL(sys_fchown);
SYSCALL(sys_lchown);
SYSCALL(sys_fchownat);
SYSCALL(sys_utimensat);
SYSCALL(sys_mkdir);
SYSCALL(sys_mkdirat);
SYSCALL(sys_rmdir);
SYSCALL(sys_unlink);
SYSCALL(sys_unlinkat);
SYSCALL(sys_rename);
SYSCALL(sys_renameat);
SYSCALL(sys_renameat2);
SYSCALL(sys_link);
SYSCALL(sys_linkat);
SYSCALL(sys_symlink);
SYSCALL(sys_symlinkat);
SYSCALL(sys_readlink);
SYSCALL(sys_readlinkat);
SYSCALL(sys_truncate);
SYSCALL(sys_ftruncate);

// Process syscalls (src/kernel/syscall/proc.c).
SYSCALL(sys_fork);
SYSCALL(sys_execve);
SYSCALL(sys_wait4);
SYSCALL(sys_getppid);
SYSCALL(sys_chdir);
SYSCALL(sys_fchdir);
SYSCALL(sys_getcwd);
SYSCALL(sys_getrlimit);
SYSCALL(sys_setrlimit);
SYSCALL(sys_prlimit64);
SYSCALL(sys_sysinfo);
SYSCALL(sys_times);
SYSCALL(sys_getrusage);
SYSCALL(sys_sync);

// Signals, process groups and sessions (src/kernel/proc/signal.c).
SYSCALL(sys_rt_sigaction);
SYSCALL(sys_rt_sigprocmask);
SYSCALL(sys_rt_sigsuspend);
SYSCALL(sys_pause);
SYSCALL(sys_rt_sigreturn);
SYSCALL(sys_kill);
SYSCALL(sys_tkill);
SYSCALL(sys_tgkill);
SYSCALL(sys_setpgid);
SYSCALL(sys_getpgid);
SYSCALL(sys_getpgrp);
SYSCALL(sys_setsid);
SYSCALL(sys_getsid);

// Clocks and sleeping (src/kernel/syscall/time.c).
SYSCALL(sys_clock_gettime);
SYSCALL(sys_clock_getres);
SYSCALL(sys_clock_settime);
SYSCALL(sys_clock_nanosleep);
SYSCALL(sys_nanosleep);
SYSCALL(sys_gettimeofday);
SYSCALL(sys_settimeofday);
SYSCALL(sys_time);

// Credentials (src/kernel/proc/cred.c).
SYSCALL(sys_getuid);
SYSCALL(sys_geteuid);
SYSCALL(sys_getgid);
SYSCALL(sys_getegid);
SYSCALL(sys_setuid);
SYSCALL(sys_setgid);
SYSCALL(sys_setreuid);
SYSCALL(sys_setregid);
SYSCALL(sys_setresuid);
SYSCALL(sys_getresuid);
SYSCALL(sys_setresgid);
SYSCALL(sys_getresgid);
SYSCALL(sys_setfsuid);
SYSCALL(sys_setfsgid);
SYSCALL(sys_getgroups);
SYSCALL(sys_setgroups);

// Kernel log (src/kernel/log.c).
SYSCALL(sys_syslog);

// Copy a NUL-terminated user string (at most max bytes incl. NUL),
// checking every page it touches. 0, -EFAULT or -ENAMETOOLONG.
long copy_user_str(char *dst, long src, size_t max);

// Current directory of the calling process.
struct inode *cwd_inode(void);

int init_syscalls(void);
void syscall_dispatch(registers_t *regs);

#endif /* SYSCALL_H */
