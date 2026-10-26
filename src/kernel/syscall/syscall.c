#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/hal/io.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>
#include <kernel/video/vga.h>

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

static long sys_exit(long status, long a2, long a3, long a4, long a5, long a6) {
    (void)status; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    scheduler_exit();
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
    [SYS_ioctl]           = sys_ioctl,
    [SYS_readv]           = sys_readv,
    [SYS_writev]          = sys_writev,
    [SYS_access]          = sys_access,
    [SYS_dup]             = sys_dup,
    [SYS_dup2]            = sys_dup2,
    [SYS_getpid]          = sys_getpid,
    [SYS_exit]            = sys_exit,
    [SYS_arch_prctl]      = sys_arch_prctl,
    [SYS_set_tid_address] = sys_set_tid_address,
    [SYS_exit_group]      = sys_exit,
    [SYS_openat]          = sys_openat,
    [SYS_newfstatat]      = sys_newfstatat,
    [SYS_faccessat]       = sys_faccessat,
    [SYS_dup3]            = sys_dup3,
};

void syscall_dispatch(registers_t *r) {
    if (r->rax >= LEN(syscalls) || syscalls[r->rax] == NULL) {
        r->rax = (uint64_t)-ENOSYS;
        return;
    }
    r->rax = (uint64_t)syscalls[r->rax](r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9);
}

int init_syscalls(void) {
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);
    /* sysret: CS = 0x10 + 16, SS = 0x10 + 8; syscall: CS = 0x08, SS = 0x10 */
    wrmsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));
    wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)syscall_entry);
    wrmsr(MSR_SFMASK, SFMASK);
    return 0;
}
