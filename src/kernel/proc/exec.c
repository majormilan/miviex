#include <kernel/proc/exec.h>
#include <kernel/proc/scheduler.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/vmm.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/log.h>

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
 * Linux initial stack, from rsp upwards: argc, argv[], NULL, envp[] (empty),
 * NULL, auxv pairs, AT_NULL. Strings and AT_RANDOM bytes sit above it.
 */
static int build_stack(address_space_t *as, const char *path, uint64_t entry,
                       uint64_t phdr, uint16_t phnum, uint64_t *rsp) {
    uint64_t lo = USER_STACK_TOP - USER_STACK_SIZE;
    uint64_t sp = USER_STACK_TOP;
    uint64_t v[32];
    size_t n = 0;
    uint64_t lo32, hi32;

    if (vmm_alloc_range(as, lo, USER_STACK_TOP, PAGE_PRESENT | PAGE_USER | PAGE_WRITE) < 0) {
        return -1;
    }

    size_t plen = strlen(path) + 1;
    sp -= plen;
    uint64_t argv0 = sp;
    vmm_copy_to(as, argv0, path, plen);

    /* No entropy source yet: the TSC is better than a constant. */
    asm volatile("rdtsc" : "=a"(lo32), "=d"(hi32));
    uint64_t rnd[2] = { (hi32 << 32 | lo32) * 6364136223846793005ULL, entry ^ lo32 };
    sp -= sizeof(rnd);
    uint64_t random = sp;
    vmm_copy_to(as, random, rnd, sizeof(rnd));

    v[n++] = 1;      /* argc */
    v[n++] = argv0;
    v[n++] = 0;      /* end of argv */
    v[n++] = 0;      /* end of envp */
    v[n++] = AT_PHDR;   v[n++] = phdr;
    v[n++] = AT_PHENT;  v[n++] = sizeof(Elf64_Phdr);
    v[n++] = AT_PHNUM;  v[n++] = phnum;
    v[n++] = AT_PAGESZ; v[n++] = PAGE_SIZE;
    v[n++] = AT_BASE;   v[n++] = 0;
    v[n++] = AT_ENTRY;  v[n++] = entry;
    v[n++] = AT_UID;    v[n++] = 0;
    v[n++] = AT_EUID;   v[n++] = 0;
    v[n++] = AT_GID;    v[n++] = 0;
    v[n++] = AT_EGID;   v[n++] = 0;
    v[n++] = AT_SECURE; v[n++] = 0;
    v[n++] = AT_RANDOM; v[n++] = random;
    v[n++] = AT_EXECFN; v[n++] = argv0;
    v[n++] = AT_NULL;   v[n++] = 0;

    /* The ABI wants rsp 16-byte aligned at entry, pointing at argc. */
    sp = (sp - n * sizeof(uint64_t)) & ~0xFULL;
    if (vmm_copy_to(as, sp, v, n * sizeof(uint64_t)) < 0) {
        return -1;
    }
    *rsp = sp;
    return 0;
}

process_t *process_exec(const char *path, uint8_t priority) {
    char buf[128];
    inode_t *node;
    uint8_t *img;
    address_space_t *as;
    uint64_t entry, phdr, rsp;
    uint16_t phnum;
    process_t *proc;

    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    node = vfs_lookup(fs_root->inode, buf);
    if (node == NULL || node->read == NULL || node->length == 0) {
        klog(LOG_FAIL, "exec", "%s: not found", path);
        return NULL;
    }

    img = k_malloc(node->length);
    if (img == NULL || node->read(node, 0, node->length, img) != node->length) {
        klog(LOG_FAIL, "exec", "%s: read failed", path);
        k_free(img);
        return NULL;
    }

    as = vmm_create_address_space();
    if (as == NULL || load_elf(as, img, node->length, &entry, &phdr, &phnum) < 0 ||
        build_stack(as, path, entry, phdr, phnum, &rsp) < 0) {
        klog(LOG_FAIL, "exec", "%s: not a loadable static ELF64 executable", path);
        k_free(img);
        if (as != NULL) {
            vmm_destroy_address_space(as);
        }
        return NULL;
    }
    k_free(img);

    proc = process_create_user(as, entry, rsp, priority);
    if (proc == NULL) {
        vmm_destroy_address_space(as);
    }
    return proc;
}
