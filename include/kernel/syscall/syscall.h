#ifndef SYSCALL_H
#define SYSCALL_H

#include <kernel/hal/isr.h>

#define MAX_SYSCALLS 1

extern void *syscalls[MAX_SYSCALLS];

int init_syscalls();

#endif