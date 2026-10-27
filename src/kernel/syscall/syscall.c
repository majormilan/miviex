#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/hal/io.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/video/vga.h>
#include <kernel/log.h>
#include <kernel/hal/power.h>
#include <kernel/version.h>
#include <kernel/drivers/tty.h>
#include <kernel/drivers/random.h>
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

#define UTS_LEN 65

static char hostname[UTS_LEN] = "(none)";
static char domainname[UTS_LEN] = "(none)";

static long sys_uname(long ubuf, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char u[6][UTS_LEN];

    if (!vmm_user_ok((uint64_t)ubuf, sizeof(u), true)) {
        return -EFAULT;
    }
    memset(u, 0, sizeof(u));
    strcpy(u[0], UTS_SYSNAME);
    strcpy(u[1], hostname);
    strcpy(u[2], UTS_RELEASE);
    strcpy(u[3], UTS_VERSION);
    strcpy(u[4], UTS_MACHINE);
    strcpy(u[5], domainname);
    memcpy((void *)ubuf, u, sizeof(u));
    return 0;
}

static long set_name(char *dst, long uname, long len) {
    if (len < 0 || len >= UTS_LEN) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)uname, (uint64_t)len, false)) {
        return -EFAULT;
    }
    memcpy(dst, (void *)uname, (size_t)len);
    dst[len] = '\0';
    return 0;
}

static long sys_sethostname(long uname, long len, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return set_name(hostname, uname, len);
}

static long sys_setdomainname(long uname, long len, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return set_name(domainname, uname, len);
}

/* No sockets yet. EAFNOSUPPORT is what libcs expect for a missing
 * family: musl then skips nscd and reads /etc/passwd and /etc/group. */
static long sys_socket(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return -EAFNOSUPPORT;
}

/* Reset the caller's terminal for the next login (getty). Root only. */
static long sys_vhangup(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    if (p->euid != 0) {
        return -EPERM;
    }
    if (p->ctty != NULL) {
        tty_hangup(p->ctty);
    }
    return 0;
}

/* Never blocks: the generator is seeded at boot. Flags are accepted. */
static long sys_getrandom(long ubuf, long len, long flags, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (flags & ~7L) {
        return -EINVAL;
    }
    if (len < 0) {
        return -EINVAL;
    }
    if (len > 33554431) {
        len = 33554431; /* Linux's per-call cap */
    }
    if (!vmm_user_ok((uint64_t)ubuf, (uint64_t)len, true)) {
        return -EFAULT;
    }
    random_bytes((void *)ubuf, (size_t)len);
    return len;
}

#define LINUX_REBOOT_MAGIC1       0xFEE1DEAD
#define LINUX_REBOOT_CMD_RESTART  0x01234567
#define LINUX_REBOOT_CMD_HALT     0xCDEF0123
#define LINUX_REBOOT_CMD_CAD_ON   0x89ABCDEF
#define LINUX_REBOOT_CMD_CAD_OFF  0x00000000
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321FEDC
#define LINUX_REBOOT_CMD_RESTART2 0xA1B2C3D4

/* reboot(2): root only, with Linux's magic numbers. Userspace syncs. */
static long sys_reboot(long magic1, long magic2, long cmd, long arg, long a5, long a6) {
    (void)arg; (void)a5; (void)a6;
    uint32_t m2 = (uint32_t)magic2;

    if (scheduler_current()->euid != 0) {
        return -EPERM;
    }
    if ((uint32_t)magic1 != LINUX_REBOOT_MAGIC1 ||
        (m2 != 672274793 && m2 != 85072278 && m2 != 369367448 && m2 != 537993216)) {
        return -EINVAL;
    }
    switch ((uint32_t)cmd) {
    case LINUX_REBOOT_CMD_RESTART:
    case LINUX_REBOOT_CMD_RESTART2:
        power_restart();
    case LINUX_REBOOT_CMD_HALT:
        power_halt();
    case LINUX_REBOOT_CMD_POWER_OFF:
        power_off();
    case LINUX_REBOOT_CMD_CAD_ON:
        power_set_cad(1);
        return 0;
    case LINUX_REBOOT_CMD_CAD_OFF:
        power_set_cad(0);
        return 0;
    }
    return -EINVAL;
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
    [SYS_lstat]           = sys_lstat,
    [SYS_poll]            = sys_poll,
    [SYS_lseek]           = sys_lseek,
    [SYS_mmap]            = sys_mmap,
    [SYS_mprotect]        = sys_mprotect,
    [SYS_munmap]          = sys_munmap,
    [SYS_brk]             = sys_brk,
    [SYS_rt_sigaction]    = sys_rt_sigaction,
    [SYS_rt_sigprocmask]  = sys_rt_sigprocmask,
    [SYS_rt_sigsuspend]   = sys_rt_sigsuspend,
    [SYS_pause]           = sys_pause,
    [SYS_rt_sigreturn]    = sys_rt_sigreturn,
    [SYS_ioctl]           = sys_ioctl,
    [SYS_pread64]         = sys_pread64,
    [SYS_pwrite64]        = sys_pwrite64,
    [SYS_readv]           = sys_readv,
    [SYS_pipe]            = sys_pipe,
    [SYS_writev]          = sys_writev,
    [SYS_access]          = sys_access,
    [SYS_dup]             = sys_dup,
    [SYS_dup2]            = sys_dup2,
    [SYS_nanosleep]       = sys_nanosleep,
    [SYS_getpid]          = sys_getpid,
    [SYS_socket]          = sys_socket,
    [SYS_fork]            = sys_fork,
    [SYS_vfork]           = sys_fork,
    [SYS_execve]          = sys_execve,
    [SYS_exit]            = sys_exit,
    [SYS_wait4]           = sys_wait4,
    [SYS_kill]            = sys_kill,
    [SYS_uname]           = sys_uname,
    [SYS_fcntl]           = sys_fcntl,
    [SYS_getcwd]          = sys_getcwd,
    [SYS_chdir]           = sys_chdir,
    [SYS_fchdir]          = sys_fchdir,
    [SYS_rename]          = sys_rename,
    [SYS_mkdir]           = sys_mkdir,
    [SYS_rmdir]           = sys_rmdir,
    [SYS_link]            = sys_link,
    [SYS_unlink]          = sys_unlink,
    [SYS_symlink]         = sys_symlink,
    [SYS_readlink]        = sys_readlink,
    [SYS_chmod]           = sys_chmod,
    [SYS_fchmod]          = sys_fchmod,
    [SYS_chown]           = sys_chown,
    [SYS_fchown]          = sys_fchown,
    [SYS_lchown]          = sys_lchown,
    [SYS_umask]           = sys_umask,
    [SYS_fsync]           = sys_fsync,
    [SYS_fdatasync]       = sys_fsync,
    [SYS_getdents]        = sys_getdents,
    [SYS_truncate]        = sys_truncate,
    [SYS_ftruncate]       = sys_ftruncate,
    [SYS_gettimeofday]    = sys_gettimeofday,
    [SYS_getrlimit]       = sys_getrlimit,
    [SYS_getrusage]       = sys_getrusage,
    [SYS_sysinfo]         = sys_sysinfo,
    [SYS_times]           = sys_times,
    [SYS_getuid]          = sys_getuid,
    [SYS_syslog]          = sys_syslog,
    [SYS_getgid]          = sys_getgid,
    [SYS_setuid]          = sys_setuid,
    [SYS_setgid]          = sys_setgid,
    [SYS_geteuid]         = sys_geteuid,
    [SYS_getegid]         = sys_getegid,
    [SYS_setpgid]         = sys_setpgid,
    [SYS_getppid]         = sys_getppid,
    [SYS_getpgrp]         = sys_getpgrp,
    [SYS_setreuid]        = sys_setreuid,
    [SYS_setregid]        = sys_setregid,
    [SYS_getgroups]       = sys_getgroups,
    [SYS_setgroups]       = sys_setgroups,
    [SYS_setresuid]       = sys_setresuid,
    [SYS_getresuid]       = sys_getresuid,
    [SYS_setresgid]       = sys_setresgid,
    [SYS_getresgid]       = sys_getresgid,
    [SYS_statfs]          = sys_statfs,
    [SYS_fstatfs]         = sys_fstatfs,
    [SYS_setfsuid]        = sys_setfsuid,
    [SYS_vhangup]         = sys_vhangup,
    [SYS_setfsgid]        = sys_setfsgid,
    [SYS_setsid]          = sys_setsid,
    [SYS_getpgid]         = sys_getpgid,
    [SYS_getsid]          = sys_getsid,
    [SYS_setrlimit]       = sys_setrlimit,
    [SYS_sync]            = sys_sync,
    [SYS_mount]           = sys_mount,
    [SYS_umount2]         = sys_umount2,
    [SYS_reboot]          = sys_reboot,
    [SYS_settimeofday]    = sys_settimeofday,
    [SYS_sethostname]     = sys_sethostname,
    [SYS_setdomainname]   = sys_setdomainname,
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
    [SYS_mkdirat]         = sys_mkdirat,
    [SYS_fchownat]        = sys_fchownat,
    [SYS_newfstatat]      = sys_newfstatat,
    [SYS_unlinkat]        = sys_unlinkat,
    [SYS_renameat]        = sys_renameat,
    [SYS_linkat]          = sys_linkat,
    [SYS_symlinkat]       = sys_symlinkat,
    [SYS_readlinkat]      = sys_readlinkat,
    [SYS_fchmodat]        = sys_fchmodat,
    [SYS_faccessat]       = sys_faccessat,
    [SYS_ppoll]           = sys_ppoll,
    [SYS_utimensat]       = sys_utimensat,
    [SYS_dup3]            = sys_dup3,
    [SYS_pipe2]           = sys_pipe2,
    [SYS_preadv]          = sys_preadv,
    [SYS_pwritev]         = sys_pwritev,
    [SYS_prlimit64]       = sys_prlimit64,
    [SYS_getrandom]       = sys_getrandom,
    [SYS_renameat2]       = sys_renameat2,
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
