#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
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
            if (p == me || p->ppid != me->pid || (pid > 0 && p->pid != (uint32_t)pid)) {
                continue; /* no process groups yet: 0 and < -1 mean any child */
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
