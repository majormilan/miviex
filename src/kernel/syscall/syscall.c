#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/hal/io.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/video/vga.h>
#include <kernel/log.h>
#include <kernel/libc/string.h>

#define LEN(a) (sizeof(a) / sizeof((a)[0]))

#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_SFMASK 0xC0000084
#define EFER_SCE   1

/* TF, IF, DF, NT, AC: cleared on entry so the kernel starts in a known state. */
#define SFMASK (0x100 | 0x200 | 0x400 | 0x4000 | 0x40000)

extern void syscall_entry(void);

typedef long (*syscall_fn_t)(long, long, long, long, long, long);

#define MSR_FS_BASE    0xC0000100
#define ARCH_SET_FS    0x1002
#define ARCH_GET_FS    0x1003

static long sys_getpid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->pid;
}

static long sys_arch_prctl(long code, long addr, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    switch (code) {
    case ARCH_SET_FS:
        if ((uint64_t)addr >= USER_END) {
            return -EPERM;
        }
        p->fs_base = (uint64_t)addr;
        wrmsr(MSR_FS_BASE, p->fs_base);
        return 0;
    case ARCH_GET_FS:
        if (!vmm_user_ok((uint64_t)addr, sizeof(uint64_t), true)) {
            return -EFAULT;
        }
        *(uint64_t *)addr = p->fs_base;
        return 0;
    }
    return -EINVAL;
}

/* No threads yet: the clear-on-exit address is accepted and ignored. */
static long sys_set_tid_address(long tidptr, long a2, long a3, long a4, long a5, long a6) {
    (void)tidptr; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->pid;
}

/* Single user: everyone is root. */
static long sys_getuid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0;
}

/* Everything lives in RAM; there is nothing to flush. */
static long sys_fsync(long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return fd < 0 || fd >= MAX_FDS || scheduler_current()->fds[fd] == NULL ? -EBADF : 0;
}

static long sys_exit(long status, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    scheduler_exit((status & 0xFF) << 8);
}

static const syscall_fn_t syscalls[] = {
    [SYS_read]            = sys_read,
    [SYS_write]           = sys_write,
    [SYS_open]            = sys_open,
    [SYS_close]           = sys_close,
    [SYS_stat]            = sys_stat,
    [SYS_fstat]           = sys_fstat,
    [SYS_lstat]           = sys_stat,
    [SYS_lseek]           = sys_lseek,
    [SYS_mmap]            = sys_mmap,
    [SYS_mprotect]        = sys_mprotect,
    [SYS_munmap]          = sys_munmap,
    [SYS_brk]             = sys_brk,
    [SYS_rt_sigaction]    = sys_rt_sigaction,
    [SYS_rt_sigprocmask]  = sys_rt_sigprocmask,
    [SYS_rt_sigreturn]    = sys_rt_sigreturn,
    [SYS_ioctl]           = sys_ioctl,
    [SYS_readv]           = sys_readv,
    [SYS_pipe]            = sys_pipe,
    [SYS_writev]          = sys_writev,
    [SYS_access]          = sys_access,
    [SYS_dup]             = sys_dup,
    [SYS_dup2]            = sys_dup2,
    [SYS_nanosleep]       = sys_nanosleep,
    [SYS_getpid]          = sys_getpid,
    [SYS_fork]            = sys_fork,
    [SYS_vfork]           = sys_fork,
    [SYS_execve]          = sys_execve,
    [SYS_exit]            = sys_exit,
    [SYS_wait4]           = sys_wait4,
    [SYS_kill]            = sys_kill,
    [SYS_fcntl]           = sys_fcntl,
    [SYS_getcwd]          = sys_getcwd,
    [SYS_chdir]           = sys_chdir,
    [SYS_fchdir]          = sys_fchdir,
    [SYS_fsync]           = sys_fsync,
    [SYS_fdatasync]       = sys_fsync,
    [SYS_getdents]        = sys_getdents,
    [SYS_gettimeofday]    = sys_gettimeofday,
    [SYS_getuid]          = sys_getuid,
    [SYS_getgid]          = sys_getuid,
    [SYS_geteuid]         = sys_getuid,
    [SYS_getegid]         = sys_getuid,
    [SYS_setpgid]         = sys_setpgid,
    [SYS_getppid]         = sys_getppid,
    [SYS_getpgrp]         = sys_getpgrp,
    [SYS_setsid]          = sys_setsid,
    [SYS_getpgid]         = sys_getpgid,
    [SYS_getsid]          = sys_getsid,
    [SYS_settimeofday]    = sys_settimeofday,
    [SYS_tkill]           = sys_tkill,
    [SYS_time]            = sys_time,
    [SYS_arch_prctl]      = sys_arch_prctl,
    [SYS_getdents64]      = sys_getdents64,
    [SYS_set_tid_address] = sys_set_tid_address,
    [SYS_clock_settime]   = sys_clock_settime,
    [SYS_clock_gettime]   = sys_clock_gettime,
    [SYS_clock_getres]    = sys_clock_getres,
    [SYS_clock_nanosleep] = sys_clock_nanosleep,
    [SYS_exit_group]      = sys_exit,
    [SYS_tgkill]          = sys_tgkill,
    [SYS_openat]          = sys_openat,
    [SYS_newfstatat]      = sys_newfstatat,
    [SYS_faccessat]       = sys_faccessat,
    [SYS_dup3]            = sys_dup3,
    [SYS_pipe2]           = sys_pipe2,
};

void syscall_dispatch(registers_t *r) {
    r->int_no = r->rax; /* kept for restarting after a signal */
    if (r->rax >= LEN(syscalls) || syscalls[r->rax] == NULL) {
        klog(LOG_DEBUG, "syscall", "pid %d: unimplemented syscall %lu",
             scheduler_current()->pid, r->rax);
        r->rax = (uint64_t)-ENOSYS;
    } else {
        r->rax = (uint64_t)syscalls[r->rax](r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9);
    }
    signal_deliver(r, true);
}

int init_syscalls(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    /* sysret: CS = 0x10 + 16, SS = 0x10 + 8; syscall: CS = 0x08, SS = 0x10 */
    wrmsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    wrmsr(MSR_SFMASK, SFMASK);
    return 0;
}
