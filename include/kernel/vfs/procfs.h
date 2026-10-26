#ifndef PROCFS_H
#define PROCFS_H

#include <kernel/vfs/vfs.h>

dentry_t *procfs_mount(void);     /* root dentry to mount, NULL if mounted */
void procfs_unmounted(void);
void procfs_forget(uint32_t pid); /* a process was reaped */

#endif /* PROCFS_H */
