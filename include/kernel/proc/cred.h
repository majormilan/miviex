#ifndef CRED_H
#define CRED_H

#include <kernel/types.h>
#include <kernel/vfs/vfs.h>
#include <stdint.h>

#define NGROUPS_MAX 32

/* Access bits, as in the rwx triplets. */
#define MAY_EXEC  1
#define MAY_WRITE 2
#define MAY_READ  4

#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000

struct process;

/* The kernel itself (no current process, e.g. at boot) acts as root. */
uint32_t cred_euid(void);
uint32_t cred_egid(void);
bool cred_in_group(uint32_t gid);

/* Linux generic permission check against the effective ids (or the real
 * ones, for access()). Root passes everything except exec of a file
 * with no x bit at all. 0 or -EACCES. */
long cred_permission(inode_t *n, int mask);
long cred_access(inode_t *n, int mask);

/* Owner of n, or root: may chmod, set arbitrary times, etc. */
bool cred_owns(inode_t *n);

/* Sticky directory: only the owner of the file or directory, or root,
 * may remove or rename an entry. */
bool cred_may_delete(inode_t *dir, inode_t *victim);

/* May the current process signal p? */
bool cred_may_signal(struct process *p, int sig);

#endif /* CRED_H */
