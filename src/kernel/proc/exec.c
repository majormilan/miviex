#include <kernel/proc/exec.h>
#include <kernel/proc/scheduler.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/log.h>
#include <kernel/fcntl.h>
#include <kernel/proc/cred.h>
#include <kernel/drivers/tty.h>
#include <kernel/errno.h>
#include <kernel/hal/gdt.h>
#include <kernel/hal/io.h>

#define MSR_FS_BASE 0xC0000100

#define ELFMAG0    0x7F
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC    2
#define ET_DYN     3
#define EM_X86_64  62
#define PT_LOAD    1
#define PT_INTERP  3
#define PT_PHDR    6
#define PF_W       2

#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_ENTRY   9
#define AT_UID     11
#define AT_EUID    12
#define AT_GID     13
#define AT_EGID    14
#define AT_SECURE  23
#define AT_RANDOM  25
#define AT_EXECFN  31

/* Static PIE has no fixed address; load it where ld puts ET_EXEC. */
#define PIE_BASE       0x400000ULL
#define USER_STACK_TOP USER_END

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

static int load_elf(address_space_t *as, const uint8_t *img, size_t len,
                    uint64_t *entry, uint64_t *phdr, uint16_t *phnum) {
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)img;
    uint64_t bias;

    if (len < sizeof(*eh) || eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F' ||
        eh->e_ident[4] != ELFCLASS64 || eh->e_ident[5] != ELFDATA2LSB ||
        eh->e_machine != EM_X86_64 || eh->e_phentsize != sizeof(Elf64_Phdr) ||
        eh->e_phoff > len || eh->e_phnum > (len - eh->e_phoff) / sizeof(Elf64_Phdr)) {
        return -1;
    }
    if (eh->e_type == ET_EXEC) {
        bias = 0;
    } else if (eh->e_type == ET_DYN) {
        bias = PIE_BASE;
    } else {
        return -1;
    }

    const Elf64_Phdr *ph = (const Elf64_Phdr *)(img + eh->e_phoff);
    *phdr = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *p = &ph[i];
        uint64_t va = p->p_vaddr + bias;

        if (p->p_type == PT_INTERP) {
            return -1; /* dynamic linking is not supported */
        }
        if (p->p_type == PT_PHDR) {
            *phdr = va;
        }
        if (p->p_type != PT_LOAD || p->p_memsz == 0) {
            continue;
        }
        if (p->p_filesz > p->p_memsz || p->p_offset > len ||
            p->p_filesz > len - p->p_offset ||
            va + p->p_memsz < va || va + p->p_memsz > MMAP_TOP) {
            return -1;
        }
        uint64_t flags = PAGE_PRESENT | PAGE_USER | (p->p_flags & PF_W ? PAGE_WRITE : 0);
        if (vmm_alloc_range(as, va, va + p->p_memsz, flags) < 0 ||
            vmm_copy_to(as, va, img + p->p_offset, p->p_filesz) < 0) {
            return -1;
        }
        if (align_up_page(va + p->p_memsz) > as->brk_start) {
            as->brk_start = align_up_page(va + p->p_memsz);
        }
        if (*phdr == 0 && eh->e_phoff >= p->p_offset &&
            eh->e_phoff < p->p_offset + p->p_filesz) {
            *phdr = va + (eh->e_phoff - p->p_offset);
        }
    }
    as->brk = as->brk_start;
    *entry = eh->e_entry + bias;
    *phnum = eh->e_phnum;
    return 0;
}

/*
 * Linux initial stack, from rsp upwards: argc, argv[], NULL, envp[], NULL,
 * auxv pairs, AT_NULL. Strings and AT_RANDOM bytes sit above it.
 */
/* The ids a new image runs with, and whether they differ (AT_SECURE). */
typedef struct {
    uint32_t uid, euid, gid, egid;
} ids_t;

static int build_stack(address_space_t *as, const char *path, char *const argv[],
                       char *const envp[], uint64_t entry, uint64_t phdr,
                       uint16_t phnum, const ids_t *id, uint64_t *rsp) {
    uint64_t lo = USER_STACK_TOP - USER_STACK_SIZE;
    uint64_t sp = USER_STACK_TOP;
    size_t argc = 0, envc = 0, n = 0, i;
    uint64_t *v, lo32, hi32;
    int ret = -1;

    while (argv[argc] != NULL) {
        argc++;
    }
    while (envp[envc] != NULL) {
        envc++;
    }
    if (vmm_alloc_range(as, lo, USER_STACK_TOP, PAGE_PRESENT | PAGE_USER | PAGE_WRITE) < 0) {
        return -1;
    }
    v = k_malloc((argc + envc + 32) * sizeof(uint64_t));
    if (v == NULL) {
        return -1;
    }

    /* Strings first, at the very top; v[] collects their user addresses. */
    v[n++] = argc;
    for (i = 0; i < argc + envc; i++) {
        const char *str = i < argc ? argv[i] : envp[i - argc];
        size_t len = strlen(str) + 1;
        if (sp - len < lo + PAGE_SIZE) {
            goto out; /* E2BIG: leave a page for the vector itself */
        }
        sp -= len;
        vmm_copy_to(as, sp, str, len);
        v[n++] = sp;
        if (i + 1 == argc) {
            v[n++] = 0; /* end of argv */
        }
    }
    if (argc == 0) {
        v[n++] = 0;
    }
    v[n++] = 0;         /* end of envp */

    size_t plen = strlen(path) + 1;
    sp -= plen;
    uint64_t execfn = sp;
    vmm_copy_to(as, execfn, path, plen);

    /* No entropy source yet: the TSC is better than a constant. */
    asm volatile("rdtsc" : "=a"(lo32), "=d"(hi32));
    uint64_t rnd[2] = { (hi32 << 32 | lo32) * 6364136223846793005ULL, entry ^ lo32 };
    sp -= sizeof(rnd);
    uint64_t random = sp;
    vmm_copy_to(as, random, rnd, sizeof(rnd));

    v[n++] = AT_PHDR;   v[n++] = phdr;
    v[n++] = AT_PHENT;  v[n++] = sizeof(Elf64_Phdr);
    v[n++] = AT_PHNUM;  v[n++] = phnum;
    v[n++] = AT_PAGESZ; v[n++] = PAGE_SIZE;
    v[n++] = AT_BASE;   v[n++] = 0;
    v[n++] = AT_ENTRY;  v[n++] = entry;
    v[n++] = AT_UID;    v[n++] = id->uid;
    v[n++] = AT_EUID;   v[n++] = id->euid;
    v[n++] = AT_GID;    v[n++] = id->gid;
    v[n++] = AT_EGID;   v[n++] = id->egid;
    v[n++] = AT_SECURE; v[n++] = id->uid != id->euid || id->gid != id->egid;
    v[n++] = AT_RANDOM; v[n++] = random;
    v[n++] = AT_EXECFN; v[n++] = execfn;
    v[n++] = AT_NULL;   v[n++] = 0;

    /* The ABI wants rsp 16-byte aligned at entry, pointing at argc. */
    sp = (sp - n * sizeof(uint64_t)) & ~0xFULL;
    if (sp < lo || vmm_copy_to(as, sp, v, n * sizeof(uint64_t)) < 0) {
        goto out;
    }
    *rsp = sp;
    ret = 0;
out:
    k_free(v);
    return ret;
}

/* Load path into a fresh address space; *err gets -errno on failure.
 * *id comes in as the caller's ids and leaves with setuid/setgid applied. */
static address_space_t *load_image(inode_t *base, const char *path, char *const argv[],
                                   char *const envp[], uint64_t *entry, uint64_t *rsp,
                                   ids_t *id, long *err) {
    char buf[128];
    inode_t *node;
    uint8_t *img;
    address_space_t *as;
    uint64_t phdr;
    uint16_t phnum;

    if (strlen(path) >= sizeof(buf)) {
        *err = -ENAMETOOLONG;
        return NULL;
    }
    strcpy(buf, path);
    node = vfs_lookup(base, buf);
    if (node == NULL) {
        *err = -ENOENT;
        return NULL;
    }
    if ((node->flags & VFS_TYPE) != VFS_FILE || node->read == NULL || node->length == 0 ||
        cred_permission(node, MAY_EXEC) < 0) {
        *err = -EACCES;
        return NULL;
    }
    if (node->mode & S_ISUID) {
        id->euid = node->uid;
    }
    if ((node->mode & (S_ISGID | 0010)) == (S_ISGID | 0010)) { /* setgid without g+x is locking */
        id->egid = node->gid;
    }

    img = k_malloc(node->length);
    if (img == NULL) {
        *err = -ENOMEM;
        return NULL;
    }
    if (node->read(node, 0, node->length, img) != (long)node->length) {
        k_free(img);
        *err = -EIO;
        return NULL;
    }

    as = vmm_create_address_space();
    if (as == NULL || load_elf(as, img, node->length, entry, &phdr, &phnum) < 0 ||
        build_stack(as, path, argv, envp, *entry, phdr, phnum, id, rsp) < 0) {
        k_free(img);
        if (as != NULL) {
            vmm_destroy_address_space(as);
        }
        *err = -ENOEXEC;
        return NULL;
    }
    k_free(img);
    return as;
}

process_t *process_exec(const char *path, uint8_t priority) {
    char *argv[] = { (char *)path, NULL };
    char *envp[] = { "PATH=/bin", "HOME=/root", "USER=root", "TERM=linux", NULL };
    uint64_t entry, rsp;
    long err;
    address_space_t *as;
    process_t *proc;
    ids_t id = { 0, 0, 0, 0 }; /* init runs as root */

    as = load_image(fs_root->inode, path, argv, envp, &entry, &rsp, &id, &err);
    if (as == NULL) {
        klog(LOG_FAIL, "exec", "%s: error %ld", path, -err);
        return NULL;
    }
    proc = process_create_user(as, entry, rsp, priority);
    if (proc == NULL) {
        vmm_destroy_address_space(as);
        return NULL;
    }

    // Its own session and group, the console as controlling terminal, and
    // stdin, stdout and stderr on it, as for Linux init.
    proc->pgid = proc->sid = proc->pid;
    proc->umask = 022;
    rlimit_init(proc);
    proc->ctty = tty_console;
    if (tty_console->sid == 0) {
        tty_console->sid = proc->sid;
        tty_console->pgrp = proc->pgid;
    }
    file_t *con = file_open(fs_root->inode, "/dev/console", O_RDWR, 0, &err);
    if (con != NULL) {
        proc->fds[0] = con;
        proc->fds[1] = file_get(con);
        proc->fds[2] = file_get(con);
    }
    return proc;
}

long process_execve(const char *path, char *const argv[], char *const envp[]) {
    process_t *p = scheduler_current();
    inode_t *base = p->cwd != NULL ? p->cwd : fs_root->inode;
    uint64_t entry, rsp;
    long err;
    address_space_t *as, *old;
    registers_t *f;
    ids_t id = { p->uid, p->euid, p->gid, p->egid };

    as = load_image(base, path, argv, envp, &entry, &rsp, &id, &err);
    if (as == NULL) {
        return err; /* the old image is untouched */
    }

    // setuid/setgid take effect; the saved ids follow, as on Linux.
    p->euid = p->suid = id.euid;
    p->egid = p->sgid = id.egid;

    old = p->as;
    p->as = as;
    vmm_switch_address_space(as);
    vmm_destroy_address_space(old);

    for (int i = 0; i < MAX_FDS; i++) {
        if (p->cloexec[i]) {
            file_put(p->fds[i]);
            p->fds[i] = NULL;
            p->cloexec[i] = 0;
        }
    }

    // Caught signals revert to default; ignored ones and the mask stay.
    for (int i = 0; i < NSIG; i++) {
        if (p->sigact[i].handler != SIG_IGN) {
            memset(&p->sigact[i], 0, sizeof(p->sigact[i]));
        }
    }

    // Fresh CPU state: no TLS, default FPU, registers zeroed.
    p->fs_base = 0;
    wrmsr(MSR_FS_BASE, 0);
    memset(p->fpu, 0, sizeof(p->fpu));
    *(uint16_t *)&p->fpu[0] = 0x37F;
    *(uint32_t *)&p->fpu[24] = 0x1F80;
    asm volatile("fxrstor (%0)" : : "r"(p->fpu) : "memory");

    f = user_frame(p);
    memset(f, 0, sizeof(*f));
    f->rip = entry;
    f->rsp = rsp;
    f->rflags = 0x202;
    f->cs = USER_CODE_SEL;
    f->ss = USER_DATA_SEL;
    return 0;
}
