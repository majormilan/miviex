#ifndef EXEC_H
#define EXEC_H

#include <kernel/proc/process.h>

// Load a static ELF64 executable from the VFS into a fresh address space
// and queue it as a ring-3 process, Linux-style (argv/envp/auxv on stack).
process_t *process_exec(const char *path, uint8_t priority);

// Replace the current process image (execve). On success the user frame is
// reset to the new entry point and 0 is returned; on failure -errno, with
// the old image intact.
long process_execve(const char *path, char *const argv[], char *const envp[]);

#endif // EXEC_H
