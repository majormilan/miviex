#ifndef EXEC_H
#define EXEC_H

#include <kernel/proc/process.h>

// Load a static ELF64 executable from the VFS into a fresh address space
// and queue it as a ring-3 process, Linux-style (argv/envp/auxv on stack).
process_t *process_exec(const char *path, uint8_t priority);

#endif // EXEC_H
