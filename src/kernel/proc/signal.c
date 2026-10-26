#include <kernel/proc/signal.h>
#include <kernel/proc/scheduler.h>
#include <kernel/syscall/syscall.h>
#include <kernel/mm/vmm.h>
#include <kernel/hal/gdt.h>
#include <kernel/errno.h>
#include <kernel/libc/string.h>
#include <kernel/proc/cred.h>

#define UNBLOCKABLE (SIGBIT(SIGKILL) | SIGBIT(SIGSTOP))
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#define SI_USER     0

/* The x86-64 Linux signal frame, so libc restorers and SA_SIGINFO
 * handlers find everything where they expect it. */
struct sigcontext {
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rdi, rsi, rbp, rbx, rdx, rax, rcx, rsp, rip, eflags;
    uint16_t cs, gs, fs, ss;
    uint64_t err, trapno, oldmask, cr2, fpstate, reserved[8];
};

struct ucontext {
    uint64_t flags, link;
    uint64_t ss_sp;
    int32_t ss_flags, pad;
    uint64_t ss_size;
    struct sigcontext mc;
    uint64_t sigmask;
};

struct siginfo {
    int32_t signo, err, code, pad;
    int32_t pid, uid;
    uint8_t rest[104];
};

struct rt_sigframe {
    uint64_t pretcode; /* sa_restorer: calls rt_sigreturn */
    struct ucontext uc;
    struct siginfo info;
};

/* Stop signals are not implemented yet; their default is ignored too. */
static bool default_ignored(int sig) {
    switch (sig) {
    case SIGCHLD: case SIGCONT: case SIGURG: case SIGWINCH:
    case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
        return true;
    }
    return false;
}

static bool discarded(process_t *p, int sig) {
    uint64_t h = p->sigact[sig - 1].handler;

    if (sig == SIGKILL) {
        return p->pid == 1;
    }
    /* init only gets signals it has asked for, as on Linux */
    return h == SIG_IGN || (h == SIG_DFL && (default_ignored(sig) || p->pid == 1));
}

void signal_send(process_t *p, int sig) {
    if (p == NULL || !p->is_user || p->state == PROCESS_TERMINATED ||
        sig < 1 || sig > NSIG || discarded(p, sig)) {
        return;
    }
    p->sig_pending |= SIGBIT(sig);
    if (p->state == PROCESS_BLOCKED && signal_pending(p)) {
        process_wake(p);
    }
}

long signal_send_pgrp(uint32_t pgrp, int sig) {
    long err = -ESRCH;

    for (process_t *p = process_list(); p != NULL; p = p->all_next) {
        if (p->is_user && p->pgid == pgrp && p->state != PROCESS_TERMINATED) {
            signal_send(p, sig);
            err = 0;
        }
    }
    return err;
}

bool signal_pending(process_t *p) {
    return (p->sig_pending & ~p->sig_blocked) != 0;
}

static int setup_frame(process_t *p, registers_t *r, int sig, k_sigaction_t *k) {
    uint64_t sp = r->rsp - 128; /* skip the red zone */
    uint64_t fp;
    struct rt_sigframe f;

    if (!(k->flags & SA_RESTORER)) {
        return -1; /* every libc provides one; without it there is no way back */
    }
    sp = (sp - sizeof(p->fpu)) & ~63ULL;
    fp = sp;
    sp = ((sp - sizeof(f)) & ~15ULL) - 8; /* as if a call pushed pretcode */
    if (!vmm_user_ok(sp, fp + sizeof(p->fpu) - sp, true)) {
        return -1;
    }

    memset(&f, 0, sizeof(f));
    f.pretcode = k->restorer;
    f.uc.mc = (struct sigcontext){
        .r8 = r->r8, .r9 = r->r9, .r10 = r->r10, .r11 = r->r11,
        .r12 = r->r12, .r13 = r->r13, .r14 = r->r14, .r15 = r->r15,
        .rdi = r->rdi, .rsi = r->rsi, .rbp = r->rbp, .rbx = r->rbx,
        .rdx = r->rdx, .rax = r->rax, .rcx = r->rcx, .rsp = r->rsp,
        .rip = r->rip, .eflags = r->rflags, .cs = (uint16_t)r->cs,
        .ss = (uint16_t)r->ss, .oldmask = p->sig_blocked, .fpstate = fp,
    };
    f.uc.sigmask = p->sig_blocked;
    f.info.signo = sig;
    f.info.code = SI_USER;

    asm volatile("fxsave (%0)" : : "r"(p->fpu) : "memory");
    memcpy((void *)fp, p->fpu, sizeof(p->fpu));
    memcpy((void *)sp, &f, sizeof(f));

    r->rip = k->handler;
    r->rsp = sp;
    r->rdi = (uint64_t)sig;
    r->rsi = sp + __builtin_offsetof(struct rt_sigframe, info);
    r->rdx = sp + __builtin_offsetof(struct rt_sigframe, uc);
    r->rax = 0;
    r->rflags &= ~0x500ULL; /* TF, DF */

    p->sig_blocked |= k->mask | (k->flags & SA_NODEFER ? 0 : SIGBIT(sig));
    p->sig_blocked &= ~UNBLOCKABLE;
    if (k->flags & SA_RESETHAND) {
        k->handler = SIG_DFL;
    }
    return 0;
}

/* A syscall is restarted by re-executing its 2-byte syscall instruction. */
static void restart(registers_t *r) {
    r->rax = r->int_no; /* the syscall number, saved by the dispatcher */
    r->rip -= 2;
}

void signal_deliver(registers_t *r, bool from_syscall) {
    process_t *p = scheduler_current();
    bool interrupted = from_syscall && (long)r->rax == -ERESTARTSYS;

    while (p->is_user && signal_pending(p)) {
        uint64_t ready = p->sig_pending & ~p->sig_blocked;
        int sig = __builtin_ctzll(ready) + 1;
        k_sigaction_t *k = &p->sigact[sig - 1];

        p->sig_pending &= ~SIGBIT(sig);
        if (k->handler == SIG_IGN || (k->handler == SIG_DFL && default_ignored(sig))) {
            continue;
        }
        if (k->handler == SIG_DFL) {
            scheduler_exit(sig); /* "terminated by signal" */
        }
        if (interrupted) {
            if (k->flags & SA_RESTART) {
                restart(r);
            } else {
                r->rax = (uint64_t)-EINTR;
            }
        }
        if (setup_frame(p, r, sig, k) < 0) {
            scheduler_exit(SIGSEGV);
        }
        return;
    }
    if (interrupted) {
        restart(r); /* nothing ran in user space: retry transparently */
    }
}

long sys_rt_sigaction(long sig, long act, long oact, long size, long a5, long a6) {
    (void)a5; (void)a6;
    process_t *p = scheduler_current();
    k_sigaction_t k;

    if (sig < 1 || sig > NSIG || size != 8) {
        return -EINVAL;
    }
    if (act != 0 && (sig == SIGKILL || sig == SIGSTOP)) {
        return -EINVAL;
    }
    if (oact != 0) {
        if (!vmm_user_ok((uint64_t)oact, sizeof(k), true)) {
            return -EFAULT;
        }
        *(k_sigaction_t *)oact = p->sigact[sig - 1];
    }
    if (act != 0) {
        if (!vmm_user_ok((uint64_t)act, sizeof(k), false)) {
            return -EFAULT;
        }
        k = *(k_sigaction_t *)act;
        k.mask &= ~UNBLOCKABLE;
        p->sigact[sig - 1] = k;
        if (discarded(p, (int)sig)) {
            p->sig_pending &= ~SIGBIT(sig); /* POSIX: ignoring drops pending */
        }
    }
    return 0;
}

long sys_rt_sigprocmask(long how, long set, long oset, long size, long a5, long a6) {
    (void)a5; (void)a6;
    process_t *p = scheduler_current();
    uint64_t m;

    if (size != 8) {
        return -EINVAL;
    }
    if (oset != 0) {
        if (!vmm_user_ok((uint64_t)oset, 8, true)) {
            return -EFAULT;
        }
        *(uint64_t *)oset = p->sig_blocked;
    }
    if (set == 0) {
        return 0;
    }
    if (!vmm_user_ok((uint64_t)set, 8, false)) {
        return -EFAULT;
    }
    m = *(uint64_t *)set;
    switch (how) {
    case SIG_BLOCK:   p->sig_blocked |= m; break;
    case SIG_UNBLOCK: p->sig_blocked &= ~m; break;
    case SIG_SETMASK: p->sig_blocked = m; break;
    default: return -EINVAL;
    }
    p->sig_blocked &= ~UNBLOCKABLE;
    return 0;
}

long sys_rt_sigreturn(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();
    registers_t *r = user_frame(p);
    struct ucontext *uc = (struct ucontext *)r->rsp; /* pretcode was popped */
    struct sigcontext *mc;

    if (!vmm_user_ok(r->rsp, sizeof(*uc), false)) {
        scheduler_exit(SIGSEGV);
    }
    mc = &uc->mc;
    if (mc->rip >= USER_END ||
        (mc->fpstate != 0 && !vmm_user_ok(mc->fpstate, sizeof(p->fpu), false))) {
        scheduler_exit(SIGSEGV); /* sysret to a non-canonical rip would fault in ring 0 */
    }

    r->r8 = mc->r8; r->r9 = mc->r9; r->r10 = mc->r10; r->r11 = mc->r11;
    r->r12 = mc->r12; r->r13 = mc->r13; r->r14 = mc->r14; r->r15 = mc->r15;
    r->rdi = mc->rdi; r->rsi = mc->rsi; r->rbp = mc->rbp; r->rbx = mc->rbx;
    r->rdx = mc->rdx; r->rax = mc->rax; r->rcx = mc->rcx; r->rsp = mc->rsp;
    r->rip = mc->rip;
    r->rflags = (mc->eflags & 0x40DD5ULL) | 0x202; /* arithmetic flags, TF, DF, AC */
    r->cs = USER_CODE_SEL;
    r->ss = USER_DATA_SEL;
    if (mc->fpstate != 0) {
        memcpy(p->fpu, (void *)mc->fpstate, sizeof(p->fpu));
        *(uint32_t *)&p->fpu[24] &= 0xFFBF; /* reserved MXCSR bits would #GP */
        asm volatile("fxrstor (%0)" : : "r"(p->fpu) : "memory");
    }
    p->sig_blocked = uc->sigmask & ~UNBLOCKABLE;
    r->int_no = (uint64_t)-1; /* the restored rax is not a restartable result */
    return (long)r->rax;
}

long sys_kill(long pid, long sig, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *me = scheduler_current();
    long err = -ESRCH;

    if (sig < 0 || sig > NSIG) {
        return -EINVAL;
    }
    /* pid > 0: that process; 0: our group; < -1: group -pid; -1: everyone
     * but init and ourselves. ESRCH if nobody matched, EPERM if nobody
     * matched that we may signal. */
    uint32_t pgrp = pid == 0 ? me->pgid : (uint32_t)-pid;
    for (process_t *p = process_list(); p != NULL; p = p->all_next) {
        bool match = pid > 0 ? p->pid == (uint32_t)pid :
                     pid == -1 ? p->pid > 1 && p != me : p->pgid == pgrp;
        if (!match || !p->is_user || p->state == PROCESS_TERMINATED) {
            continue;
        }
        if (!cred_may_signal(p, (int)sig)) {
            if (err == -ESRCH) {
                err = -EPERM;
            }
            continue;
        }
        signal_send(p, (int)sig);
        err = 0;
    }
    return err;
}

/* One thread per process: a tid is a pid. */
long sys_tkill(long tid, long sig, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return tid <= 0 ? -EINVAL : sys_kill(tid, sig, 0, 0, 0, 0);
}

long sys_tgkill(long tgid, long tid, long sig, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    return tgid != tid || tid <= 0 ? -EINVAL : sys_kill(tid, sig, 0, 0, 0, 0);
}

static process_t *self_or(long pid) {
    return pid == 0 ? scheduler_current() : process_find((uint32_t)pid);
}

static bool group_exists(uint32_t pgid, uint32_t sid) {
    for (process_t *p = process_list(); p != NULL; p = p->all_next) {
        if (p->is_user && p->pgid == pgid && p->sid == sid) {
            return true;
        }
    }
    return false;
}

long sys_setpgid(long pid, long pgid, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *me = scheduler_current();
    process_t *p = self_or(pid);

    if (pgid < 0) {
        return -EINVAL;
    }
    if (p == NULL || (p != me && p->ppid != me->pid)) {
        return -ESRCH;
    }
    if (p->sid != me->sid || p->sid == p->pid) {
        return -EPERM;
    }
    if (pgid == 0) {
        pgid = p->pid;
    }
    if ((uint32_t)pgid != p->pid && !group_exists((uint32_t)pgid, me->sid)) {
        return -EPERM;
    }
    p->pgid = (uint32_t)pgid;
    return 0;
}

long sys_getpgid(long pid, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = self_or(pid);

    return p == NULL ? -ESRCH : (long)p->pgid;
}

long sys_getpgrp(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->pgid;
}

long sys_setsid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *me = scheduler_current();

    for (process_t *p = process_list(); p != NULL; p = p->all_next) {
        if (p->is_user && p->pgid == me->pid) {
            return -EPERM; /* already a group leader */
        }
    }
    me->sid = me->pgid = me->pid;
    me->ctty = NULL;
    return me->sid;
}

long sys_getsid(long pid, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = self_or(pid);

    return p == NULL ? -ESRCH : (long)p->sid;
}
