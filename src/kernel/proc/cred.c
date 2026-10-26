#include <kernel/proc/cred.h>
#include <kernel/proc/scheduler.h>
#include <kernel/proc/signal.h>
#include <kernel/syscall/syscall.h>
#include <kernel/mm/vmm.h>
#include <kernel/errno.h>

uint32_t cred_euid(void) {
    process_t *p = scheduler_current();

    return p != NULL ? p->euid : 0;
}

uint32_t cred_egid(void) {
    process_t *p = scheduler_current();

    return p != NULL ? p->egid : 0;
}

static bool member(process_t *p, uint32_t gid, uint32_t primary) {
    if (primary == gid) {
        return true;
    }
    for (uint32_t i = 0; i < p->ngroups; i++) {
        if (p->groups[i] == gid) {
            return true;
        }
    }
    return false;
}

bool cred_in_group(uint32_t gid) {
    process_t *p = scheduler_current();

    return p == NULL || member(p, gid, p->egid);
}

static long check(inode_t *n, int mask, bool real) {
    process_t *p = scheduler_current();
    uint32_t uid, gid, perm;

    if (p == NULL) {
        return 0;
    }
    uid = real ? p->uid : p->euid;
    gid = real ? p->gid : p->egid;
    if (uid == 0) {
        bool dir = (n->flags & VFS_TYPE) == VFS_DIRECTORY;
        return (mask & MAY_EXEC) && !dir && !(n->mode & 0111) ? -EACCES : 0;
    }
    if (uid == n->uid) {
        perm = n->mode >> 6;
    } else if (member(p, n->gid, gid)) {
        perm = n->mode >> 3;
    } else {
        perm = n->mode;
    }
    return ((perm & 7) & (uint32_t)mask) == (uint32_t)mask ? 0 : -EACCES;
}

long cred_permission(inode_t *n, int mask) {
    return check(n, mask, false);
}

long cred_access(inode_t *n, int mask) {
    return check(n, mask, true);
}

bool cred_owns(inode_t *n) {
    uint32_t euid = cred_euid();

    return euid == 0 || euid == n->uid;
}

bool cred_may_delete(inode_t *dir, inode_t *victim) {
    uint32_t euid = cred_euid();

    return !(dir->mode & S_ISVTX) || euid == 0 || euid == dir->uid || euid == victim->uid;
}

/* Linux: the sender's real or effective uid matches the target's real or
 * saved uid; SIGCONT also works within a session. */
bool cred_may_signal(process_t *p, int sig) {
    process_t *me = scheduler_current();

    if (me == NULL || me->euid == 0) {
        return true;
    }
    if (sig == SIGCONT && me->sid == p->sid) {
        return true;
    }
    return me->uid == p->uid || me->uid == p->suid ||
           me->euid == p->uid || me->euid == p->suid;
}

/* --- syscalls --- */

#define KEEP ((uint32_t)-1)

long sys_getuid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->uid;
}

long sys_geteuid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->euid;
}

long sys_getgid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->gid;
}

long sys_getegid(long a1, long a2, long a3, long a4, long a5, long a6) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->egid;
}

/*
 * setres[ug]id is the general form: root may set anything; others may only
 * move between their current real, effective and saved ids. -1 keeps.
 */
static long setres(uint32_t *r, uint32_t *e, uint32_t *s, uint32_t nr, uint32_t ne,
                   uint32_t ns, bool root) {
    uint32_t cur[3] = { *r, *e, *s }, want[3] = { nr, ne, ns };

    for (int i = 0; i < 3 && !root; i++) {
        if (want[i] != KEEP && want[i] != cur[0] && want[i] != cur[1] && want[i] != cur[2]) {
            return -EPERM;
        }
    }
    if (nr != KEEP) {
        *r = nr;
    }
    if (ne != KEEP) {
        *e = ne;
    }
    if (ns != KEEP) {
        *s = ns;
    }
    return 0;
}

long sys_setresuid(long r, long e, long s, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    return setres(&p->uid, &p->euid, &p->suid, (uint32_t)r, (uint32_t)e, (uint32_t)s,
                  p->euid == 0);
}

long sys_setresgid(long r, long e, long s, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    return setres(&p->gid, &p->egid, &p->sgid, (uint32_t)r, (uint32_t)e, (uint32_t)s,
                  p->euid == 0);
}

static long put_res(long ur, long ue, long us, uint32_t r, uint32_t e, uint32_t s) {
    if (!vmm_user_ok((uint64_t)ur, 4, true) || !vmm_user_ok((uint64_t)ue, 4, true) ||
        !vmm_user_ok((uint64_t)us, 4, true)) {
        return -EFAULT;
    }
    *(uint32_t *)ur = r;
    *(uint32_t *)ue = e;
    *(uint32_t *)us = s;
    return 0;
}

long sys_getresuid(long ur, long ue, long us, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    return put_res(ur, ue, us, p->uid, p->euid, p->suid);
}

long sys_getresgid(long ur, long ue, long us, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    return put_res(ur, ue, us, p->gid, p->egid, p->sgid);
}

/* POSIX: root sets all three; others only the effective id. */
long sys_setuid(long uid, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    if (p->euid == 0) {
        p->uid = p->euid = p->suid = (uint32_t)uid;
        return 0;
    }
    return sys_setresuid(-1, uid, -1, 0, 0, 0);
}

long sys_setgid(long gid, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    if (p->euid == 0) {
        p->gid = p->egid = p->sgid = (uint32_t)gid;
        return 0;
    }
    return sys_setresgid(-1, gid, -1, 0, 0, 0);
}

/* The saved id follows the effective one when the real id is set or the
 * effective id moves away from the real one. */
long sys_setreuid(long r, long e, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();
    uint32_t ne = (uint32_t)e == KEEP ? p->euid : (uint32_t)e;
    uint32_t s = (uint32_t)r != KEEP || ne != p->uid ? ne : KEEP;

    return sys_setresuid(r, e, s, 0, 0, 0);
}

long sys_setregid(long r, long e, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();
    uint32_t ne = (uint32_t)e == KEEP ? p->egid : (uint32_t)e;
    uint32_t s = (uint32_t)r != KEEP || ne != p->gid ? ne : KEEP;

    return sys_setresgid(r, e, s, 0, 0, 0);
}

/* No separate filesystem ids: they always equal the effective ones. */
long sys_setfsuid(long uid, long a2, long a3, long a4, long a5, long a6) {
    (void)uid; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->euid;
}

long sys_setfsgid(long gid, long a2, long a3, long a4, long a5, long a6) {
    (void)gid; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    return scheduler_current()->egid;
}

long sys_getgroups(long size, long list, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    if (size < 0) {
        return -EINVAL;
    }
    if (size == 0) {
        return p->ngroups;
    }
    if ((uint32_t)size < p->ngroups) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)list, p->ngroups * 4ULL, true)) {
        return -EFAULT;
    }
    for (uint32_t i = 0; i < p->ngroups; i++) {
        ((uint32_t *)list)[i] = p->groups[i];
    }
    return p->ngroups;
}

long sys_setgroups(long size, long list, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    process_t *p = scheduler_current();

    if (p->euid != 0) {
        return -EPERM;
    }
    if (size < 0 || size > NGROUPS_MAX) {
        return -EINVAL;
    }
    if (!vmm_user_ok((uint64_t)list, (uint64_t)size * 4, false)) {
        return -EFAULT;
    }
    for (long i = 0; i < size; i++) {
        p->groups[i] = ((uint32_t *)list)[i];
    }
    p->ngroups = (uint32_t)size;
    return 0;
}
