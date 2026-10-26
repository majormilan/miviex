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
    scheduler_current()->cwd = n;
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

/* Walk dentries from the cwd up to "/", writing the path right to left. */
long sys_getcwd(long ubuf, long size, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    char tmp[256];
    size_t at = sizeof(tmp) - 1;
    dentry_t *d = (dentry_t *)cwd_inode()->ptr;

    tmp[at] = '\0';
    for (; d != NULL && d->parent != NULL; d = d->parent) {
        size_t len = strlen(d->name);
        if (len + 1 > at) {
            return -ENAMETOOLONG;
        }
        at -= len;
        memcpy(tmp + at, d->name, len);
        tmp[--at] = '/';
    }
    if (tmp[at] == '\0') {
        tmp[--at] = '/';
    }

    size_t n = sizeof(tmp) - at;
    if ((size_t)size < n) {
        return -ERANGE;
    }
    if (!vmm_user_ok((uint64_t)ubuf, n, true)) {
        return -EFAULT;
    }
    memcpy((void *)ubuf, tmp + at, n);
    return (long)n;
}
