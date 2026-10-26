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

static long sys_write(long fd, long buf, long len, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;

    if (fd != 1 && fd != 2) {
        return -EBADF; /* no per-process fd table yet */
    }
    if (len < 0) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)buf, (size_t)len)) {
        return -EFAULT;
    }
    for (long i = 0; i < len; i++) {
        terminal_putchar(((const char *)buf)[i]);
    }
    return len;
}

static long sys_exit(long status, long a2, long a3, long a4, long a5, long a6) {
    (void)status; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    scheduler_exit();
}

static const syscall_fn_t syscalls[] = {
    [SYS_write]      = sys_write,
    [SYS_exit]       = sys_exit,
    [SYS_exit_group] = sys_exit,
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
