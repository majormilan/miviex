#ifndef SIGNAL_H
#define SIGNAL_H

#include <kernel/types.h>
#include <kernel/hal/isr.h>
#include <stdint.h>

/* Linux x86-64 signal numbers and sigaction flags. */
#define NSIG      64
#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGWINCH  28

#define SIG_DFL 0
#define SIG_IGN 1

#define SA_SIGINFO   0x00000004
#define SA_RESTORER  0x04000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

#define SIGBIT(sig) (1ULL << ((sig) - 1))

/* struct sigaction as rt_sigaction passes it on x86-64. */
typedef struct {
    uint64_t handler;
    uint64_t flags;
    uint64_t restorer;
    uint64_t mask;
} k_sigaction_t;

struct process;

/* Queue sig for p; discarded if ignored. Wakes p if it sleeps. */
void signal_send(struct process *p, int sig);
/* Send to every process in a process group. 0 if any, else -ESRCH. */
long signal_send_pgrp(uint32_t pgrp, int sig);
/* A deliverable signal is pending: blocking calls should give up. */
bool signal_pending(struct process *p);
/* On the way back to ring 3: run default actions or set up a handler
 * frame, and restart or fail an interrupted syscall. */
void signal_deliver(registers_t *r, bool from_syscall);

#endif /* SIGNAL_H */
