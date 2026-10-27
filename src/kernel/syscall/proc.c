#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/mm/pmm.h>
#include <kernel/hal/clock.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/exec.h>
#include <kernel/proc/scheduler.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/proc/cred.h>

#define WNOHANG    1
#define ARG_BUF    (64 * 1024) /* argv + envp strings, like a small ARG_MAX */
#define ARG_COUNT  1024

long sys_fork(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *child = process_fork();

    return child == NULL ? -ENOMEM : (long)child->pid;
}

/* NULL-terminated user string vector into out[], strings packed at *pos. */
static long copy_strv(uint64_t uv, char **out, char **pos, char *end) {
    size_t i = 0;

    if (uv != 0) {
        for (;; i++) {
            uint64_t s;
            char *d = *pos;

            if (i == ARG_COUNT) {
                return -E2BIG;
            }
            if (!vmm_user_ok(uv + i * 8, 8, false)) {
                return -EFAULT;
            }
            s = *(uint64_t *)(uv + i * 8);
            if (s == 0) {
                break;
            }
            for (;; s++) {
                if (d == end) {
                    return -E2BIG;
                }
                if ((d == *pos || (s & (PAGE_SIZE - 1)) == 0) && !vmm_user_ok(s, 1, false)) {
                    return -EFAULT;
                }
                if ((*d++ = *(const char *)s) == '\0') {
                    break;
                }
            }
            out[i] = *pos;
            *pos = d;
        }
    }
    out[i] = NULL;
    return 0;
}

long sys_execve(long upath, long uargv, long uenvp, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    char path[128];
    char **argv = k_malloc((ARG_COUNT + 1) * sizeof(char *));
    char **envp = k_malloc((ARG_COUNT + 1) * sizeof(char *));
    char *buf = k_malloc(ARG_BUF);
    char *pos = buf;
    long err = -ENOMEM;

    if (argv == NULL || envp == NULL || buf == NULL) {
        goto out;
    }
    if ((err = copy_user_str(path, upath, sizeof(path))) < 0 ||
        (err = copy_strv((uint64_t)uargv, argv, &pos, buf + ARG_BUF)) < 0 ||
        (err = copy_strv((uint64_t)uenvp, envp, &pos, buf + ARG_BUF)) < 0) {
        goto out;
    }
    err = process_execve(path, argv, envp);
out:
    k_free(argv);
    k_free(envp);
    k_free(buf);
    return err;
}

long sys_wait4(long pid, long ustatus, long options, long rusage, long a5, long a6) {
    (void)rusage; (void)a5; (void)a6; /* no accounting yet */
    process_t *me = scheduler_current();

    if (ustatus != 0 && !vmm_user_ok((uint64_t)ustatus, sizeof(int), true)) {
        return -EFAULT;
    }
    for (;;) {
        bool any = false;

        for (process_t *p = process_list(); p != NULL; p = p->all_next) {
            /* pid > 0: that child; 0: our group; < -1: group -pid; -1: any */
            if (p == me || p->ppid != me->pid || (pid > 0 && p->pid != (uint32_t)pid) ||
                (pid == 0 && p->pgid != me->pgid) ||
                (pid < -1 && p->pgid != (uint32_t)-pid)) {
                continue;
            }
            any = true;
            if (p->state == PROCESS_TERMINATED) {
                long cpid = p->pid;
                if (ustatus != 0) {
                    *(int *)ustatus = p->exit_status;
                }
                process_reap(p);
                return cpid;
            }
        }
        if (!any) {
            return -ECHILD;
        }
        if (options & WNOHANG) {
            return 0;
        }
        if (signal_pending(me)) {
            return -ERESTARTSYS;
        }
        sleep_on(me);
    }
}

long sys_getppid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->ppid;
}

static long set_cwd(inode_t *n) {
    if (n == NULL) {
        return -ENOENT;
    }
    if ((n->flags & VFS_TYPE) != VFS_DIRECTORY) {
        return -ENOTDIR;
    }
    if (cred_permission(n, MAY_EXEC) < 0) {
        return -EACCES;
    }
    process_t *p = scheduler_current();
    vfs_iget(n);
    if (p->cwd != NULL) {
        vfs_iput(p->cwd);
    }
    p->cwd = n;
    return 0;
}

long sys_chdir(long upath, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    char path[128];
    long err = copy_user_str(path, upath, sizeof(path));

    if (err < 0) {
        return err;
    }
    return set_cwd(vfs_lookup(cwd_inode(), path));
}

long sys_fchdir(long fd, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    if (fd < 0 || fd >= MAX_FDS || p->fds[fd] == NULL) {
        return -EBADF;
    }
    return set_cwd(p->fds[fd]->inode);
}

long sys_getcwd(long ubuf, long size, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    char tmp[256];
    size_t n;

    if (vfs_dentry_path((dentry_t *)cwd_inode()->ptr, tmp, sizeof(tmp)) < 0) {
        return -ENAMETOOLONG;
    }
    n = strlen(tmp) + 1;
    if ((size_t)size < n) {
        return -ERANGE;
    }
    if (!vmm_user_ok((uint64_t)ubuf, n, true)) {
        return -EFAULT;
    }
    memcpy((void *)ubuf, tmp, n);
    return (long)n;
}

#define RLIM_NLIMITS  16
#define RLIM_INFINITY (~0ULL)
#define RLIMIT_STACK  3
#define RLIMIT_CORE   4
#define RLIMIT_NOFILE 7

/* Linux's defaults for init; everything else unlimited. */
void rlimit_init(process_t *p) {
    for (int i = 0; i < RLIM_NLIMITS; i++) {
        p->rlim[i].cur = p->rlim[i].max = RLIM_INFINITY;
    }
    p->rlim[RLIMIT_STACK].cur = USER_STACK_MAX;
    p->rlim[RLIMIT_CORE].cur = 0;
    p->rlim[RLIMIT_NOFILE].cur = p->rlim[RLIMIT_NOFILE].max = MAX_FDS;
}

/* Get and/or set one limit of pid (0: ourselves). Only root raises hard
 * limits or touches other users' processes; NOFILE is capped by MAX_FDS. */
long sys_prlimit64(long pid, long res, long unew, long uold, long a5, long a6) {
    (void)a5; (void)a6;
    process_t *me = scheduler_current();
    process_t *p = pid == 0 ? me : process_find((uint32_t)pid);
    uint64_t lim[2];

    if (p == NULL || !p->is_user) {
        return -ESRCH;
    }
    if (res < 0 || res >= RLIM_NLIMITS) {
        return -EINVAL;
    }
    if (p != me && me->euid != 0 && (me->uid != p->uid || me->uid != p->euid)) {
        return -EPERM;
    }
    if (unew != 0) {
        if (!vmm_user_ok((uint64_t)unew, sizeof(lim), false)) {
            return -EFAULT;
        }
        memcpy(lim, (void *)unew, sizeof(lim));
        if (lim[0] > lim[1]) {
            return -EINVAL;
        }
        if (lim[1] > p->rlim[res].max && me->euid != 0) {
            return -EPERM;
        }
        if (res == RLIMIT_NOFILE && lim[1] > MAX_FDS) {
            return -EPERM; /* the descriptor table is fixed */
        }
    }
    if (uold != 0) {
        if (!vmm_user_ok((uint64_t)uold, sizeof(lim), true)) {
            return -EFAULT;
        }
        memcpy((void *)uold, &p->rlim[res], sizeof(lim));
    }
    if (unew != 0) {
        p->rlim[res].cur = lim[0];
        p->rlim[res].max = lim[1];
    }
    return 0;
}

long sys_getrlimit(long res, long uold, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_prlimit64(0, res, 0, uold, 0, 0);
}

long sys_setrlimit(long res, long unew, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_prlimit64(0, res, unew, 0, 0, 0);
}

struct sysinfo {
    int64_t uptime;
    uint64_t loads[3];
    uint64_t totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    uint16_t procs, pad;
    uint32_t pad2;
    uint64_t totalhigh, freehigh;
    uint32_t mem_unit, pad3;
};

long sys_sysinfo(long uinfo, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    struct sysinfo si;
    pmm_stats_t st;

    if (!vmm_user_ok((uint64_t)uinfo, sizeof(si), true)) {
        return -EFAULT;
    }
    pmm_get_stats(&st);
    memset(&si, 0, sizeof(si));
    si.uptime = (int64_t)(clock_monotonic_ns() / NS_PER_SEC);
    for (int i = 0; i < 3; i++) {
        si.loads[i] = loadavg[i] << (16 - 11); /* SI_LOAD_SHIFT */
    }
    si.totalram = st.total_frames * PAGE_SIZE;
    si.freeram = st.free_frames * PAGE_SIZE;
    si.procs = (uint16_t)process_count();
    si.mem_unit = 1;
    *(struct sysinfo *)uinfo = si;
    return 0;
}

/* Times are in USER_HZ (100) ticks, which is also our timer rate; all CPU
 * time counts as user time. */
long sys_times(long ubuf, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    if (ubuf != 0) {
        if (!vmm_user_ok((uint64_t)ubuf, 4 * sizeof(int64_t), true)) {
            return -EFAULT;
        }
        int64_t t[4] = { (int64_t)p->cpu_ticks, 0, (int64_t)p->child_ticks, 0 };
        memcpy((void *)ubuf, t, sizeof(t));
    }
    return (long)jiffies;
}

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1

long sys_getrusage(long who, long ubuf, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();
    int64_t ru[18]; /* struct rusage: two timevals, then 14 longs */
    uint64_t ticks;

    if (who == RUSAGE_SELF || who == RUSAGE_THREAD) {
        ticks = p->cpu_ticks;
    } else if (who == RUSAGE_CHILDREN) {
        ticks = p->child_ticks;
    } else {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)ubuf, sizeof(ru), true)) {
        return -EFAULT;
    }
    memset(ru, 0, sizeof(ru));
    uint64_t us = ticks * NS_PER_TICK / 1000;
    ru[0] = (int64_t)(us / 1000000);
    ru[1] = (int64_t)(us % 1000000);
    memcpy((void *)ubuf, ru, sizeof(ru));
    return 0;
}

/* Nothing is buffered on a disk yet. */
long sys_sync(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return 0;
}
