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

/* fd 0-2 are the console until processes get file descriptor tables. */
static long console_write(long fd, uint64_t buf, uint64_t len) {
    if (fd != 1 && fd != 2) {
        return -EBADF;
    }
    if (!vmm_user_ok(buf, len)) {
        return -EFAULT;
    }
    for (uint64_t i = 0; i < len; i++) {
        terminal_putchar(((const char *)buf)[i]);
    }
    return (long)len;
}

static long sys_write(long fd, long buf, long len, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    if (len < 0) {
        return -EINVAL;
    }
    return console_write(fd, (uint64_t)buf, (uint64_t)len);
}

struct iovec {
    uint64_t base;
    uint64_t len;
};

static long sys_writev(long fd, long iov, long cnt, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    const struct iovec *v = (const struct iovec *)iov;
    long total = 0;

    if (cnt < 0 || cnt > 1024) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)iov, (uint64_t)cnt * sizeof(*v))) {
        return -EFAULT;
    }
    for (long i = 0; i < cnt; i++) {
        long r = console_write(fd, v[i].base, v[i].len);
        if (r < 0) {
            return total ? total : r;
        }
        total += r;
    }
    return total;
}

static long sys_ioctl(long fd, long req, long arg, long a4, long a5, long a6) {
    (void)fd; (void)req; (void)arg; (void)a4; (void)a5; (void)a6;
    return -ENOTTY; /* no terminal layer yet; libc then just buffers fully */
}

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
        if (!vmm_user_ok((uint64_t)addr, sizeof(uint64_t))) {
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
    [SYS_write]           = sys_write,
    [SYS_ioctl]           = sys_ioctl,
    [SYS_writev]          = sys_writev,
    [SYS_getpid]          = sys_getpid,
    [SYS_exit]            = sys_exit,
    [SYS_arch_prctl]      = sys_arch_prctl,
    [SYS_set_tid_address] = sys_set_tid_address,
    [SYS_exit_group]      = sys_exit,
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
