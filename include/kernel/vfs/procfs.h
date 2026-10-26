#ifndef PROCFS_H
#define PROCFS_H

#include <kernel/vfs/vfs.h>

dentry_t *procfs_init(void);   /* root dentry, to mount on /proc */
void procfs_forget(uint32_t pid); /* a process was reaped */

#endif /* PROCFS_H */
