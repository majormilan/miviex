#include <kernel/syscall/syscall.h>
#include <kernel/hal/clock.h>
#include <kernel/proc/scheduler.h>
#include <kernel/mm/vmm.h>
#include <kernel/errno.h>

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7
#define TIMER_ABSTIME            1

struct timespec {
    int64_t sec, nsec;
};

struct timeval {
    int64_t sec, usec;
};

static long clock_ns(long clk, uint64_t *ns) {
    switch (clk) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        *ns = clock_realtime_ns();
        return 0;
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_RAW:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_BOOTTIME: /* no suspend, so boot time is monotonic */
        *ns = clock_monotonic_ns();
        return 0;
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
        *ns = scheduler_current()->cpu_ticks * NS_PER_TICK;
        return 0;
    }
    return -EINVAL;
}

static long put_ts(long uts, uint64_t ns) {
    if (!vmm_user_ok((uint64_t)uts, sizeof(struct timespec), true)) {
        return -EFAULT;
    }
    *(struct timespec *)uts = (struct timespec){ (int64_t)(ns / NS_PER_SEC),
                                                 (int64_t)(ns % NS_PER_SEC) };
    return 0;
}

static long get_ts(long uts, uint64_t *ns) {
    struct timespec ts;

    if (!vmm_user_ok((uint64_t)uts, sizeof(ts), false)) {
        return -EFAULT;
    }
    ts = *(struct timespec *)uts;
    if (ts.sec < 0 || ts.nsec < 0 || ts.nsec >= (int64_t)NS_PER_SEC) {
        return -EINVAL;
    }
    *ns = (uint64_t)ts.sec * NS_PER_SEC + (uint64_t)ts.nsec;
    return 0;
}

long sys_clock_gettime(long clk, long uts, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    uint64_t ns;
    long err = clock_ns(clk, &ns);

    return err < 0 ? err : put_ts(uts, ns);
}

long sys_clock_getres(long clk, long uts, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    uint64_t ns;
    long err = clock_ns(clk, &ns);

    if (err < 0 || uts == 0) {
        return err;
    }
    return put_ts(uts, NS_PER_TICK);
}

long sys_clock_settime(long clk, long uts, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    uint64_t ns;
    long err;

    if (clk != CLOCK_REALTIME) {
        return -EINVAL;
    }
    if ((err = get_ts(uts, &ns)) < 0) {
        return err;
    }
    clock_set_realtime_ns(ns);
    return 0;
}

long sys_gettimeofday(long utv, long utz, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    uint64_t ns = clock_realtime_ns();

    if (utv != 0) {
        if (!vmm_user_ok((uint64_t)utv, sizeof(struct timeval), true)) {
            return -EFAULT;
        }
        *(struct timeval *)utv = (struct timeval){ (int64_t)(ns / NS_PER_SEC),
                                                   (int64_t)(ns % NS_PER_SEC / 1000) };
    }
    if (utz != 0) { /* struct timezone is obsolete: always UTC */
        if (!vmm_user_ok((uint64_t)utz, 8, true)) {
            return -EFAULT;
        }
        *(uint64_t *)utz = 0;
    }
    return 0;
}

long sys_settimeofday(long utv, long utz, long a3, long a4, long a5, long a6) {
    (void)utz; (void)a3; (void)a4; (void)a5; (void)a6;
    struct timeval tv;

    if (utv == 0) {
        return 0;
    }
    if (!vmm_user_ok((uint64_t)utv, sizeof(tv), false)) {
        return -EFAULT;
    }
    tv = *(struct timeval *)utv;
    if (tv.sec < 0 || tv.usec < 0 || tv.usec >= 1000000) {
        return -EINVAL;
    }
    clock_set_realtime_ns((uint64_t)tv.sec * NS_PER_SEC + (uint64_t)tv.usec * 1000);
    return 0;
}

long sys_time(long ut, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    int64_t t = (int64_t)(clock_realtime_ns() / NS_PER_SEC);

    if (ut != 0) {
        if (!vmm_user_ok((uint64_t)ut, sizeof(t), true)) {
            return -EFAULT;
        }
        *(int64_t *)ut = t;
    }
    return t;
}

/* Sleep until the clock reaches `until`; -EINTR with the rest left if a
 * signal handler is about to run. Resolution is one timer tick. */
static long sleep_until(long clk, uint64_t until, long urem) {
    uint64_t now;

    for (;;) {
        clock_ns(clk, &now);
        if (now >= until) {
            return 0;
        }
        if (signal_pending(scheduler_current())) {
            if (urem != 0) {
                put_ts(urem, until - now);
            }
            return -EINTR;
        }
        sleep_on((void *)&jiffies);
    }
}

long sys_clock_nanosleep(long clk, long flags, long ureq, long urem, long a5, long a6) {
    (void)a5; (void)a6;
    uint64_t req, now;
    long err;

    if (clk == CLOCK_PROCESS_CPUTIME_ID || clk == CLOCK_THREAD_CPUTIME_ID ||
        (err = clock_ns(clk, &now)) < 0) {
        return -EINVAL;
    }
    if ((err = get_ts(ureq, &req)) < 0) {
        return err;
    }
    if (flags & TIMER_ABSTIME) {
        return sleep_until(clk, req, 0);
    }
    return sleep_until(clk, now + req, urem);
}

long sys_nanosleep(long ureq, long urem, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    return sys_clock_nanosleep(CLOCK_MONOTONIC, 0, ureq, urem, 0, 0);
}
