#ifndef SYSCALL_H
#define SYSCALL_H

#include <kernel/hal/isr.h>

#define MAX_SYSCALLS 1

extern void *syscalls[MAX_SYSCALLS];

void init_syscalls();

#endif