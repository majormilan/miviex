#include <kernel/syscall/syscall.h>
#include <kernel/errno.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <kernel/proc/scheduler.h>

#define PROT_WRITE          0x2
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_FIXED_NOREPLACE 0x100000

/* Lowest address a fixed mapping may use (Linux mmap_min_addr default). */

/*
 * All memory is allocated eagerly; there is no demand paging yet, so a
 * large mmap costs its full size in frames up front.
 */

static address_space_t *cur_as(void) {
    return scheduler_current()->as;
}

/* PROT_NONE keeps the frame but drops PAGE_USER, so any access faults. */
static uint64_t prot_flags(long prot) {
    return PAGE_PRESENT | (prot ? PAGE_USER : 0) | (prot & PROT_WRITE ? PAGE_WRITE : 0);
}

static bool user_range(uint64_t addr, uint64_t len) {
    return (addr & (PAGE_SIZE - 1)) == 0 && len != 0 &&
           addr + len > addr && addr + len <= USER_END;
}

/* Top-down first fit below the hint, above the heap's start. */
static uint64_t find_free(address_space_t *as, uint64_t len) {
    uint64_t top = as->mmap_hint;

    while (top >= as->brk_start + len) {
        uint64_t lo = top - len, p;

        for (p = top; p > lo; p -= PAGE_SIZE) {
            if (vmm_translate(as, p - PAGE_SIZE) != PHYS_ADDR_INVALID) {
                break;
            }
        }
        if (p == lo) {
            as->mmap_hint = lo;
            return lo;
        }
        top = p - PAGE_SIZE;
    }
    return 0;
}

long sys_brk(long addr, long a2, long a3, long a4, long a5, long a6) {
    (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
    address_space_t *as = cur_as();
    uint64_t want = (uint64_t)addr;
    uint64_t oldp = align_up_page(as->brk);
    uint64_t newp = align_up_page(want);

    /* Linux returns the current break on any failure, incl. brk(0). */
    if (want < as->brk_start || want > MMAP_TOP) {
        return (long)as->brk;
    }
    if (newp > oldp) {
        if (!vmm_range_is_free(as, oldp, newp)) {
            return (long)as->brk;
        }
        if (vmm_alloc_range(as, oldp, newp, PAGE_PRESENT | PAGE_USER | PAGE_WRITE) < 0) {
            vmm_free_range(as, oldp, newp);
            return (long)as->brk;
        }
    } else if (newp < oldp) {
        vmm_free_range(as, newp, oldp);
    }
    as->brk = want;
    return (long)want;
}

long sys_mmap(long addr, long len, long prot, long flags, long fd, long off) {
    (void)fd;
    address_space_t *as = cur_as();
    uint64_t size = align_up_page((uint64_t)len);
    uint64_t va;

    if (len <= 0 || size == 0 || (off & (PAGE_SIZE - 1))) {
        return -EINVAL;
    }
    if (!(flags & MAP_ANONYMOUS)) {
        return -EBADF; /* file-backed mappings come in phase 11 */
    }

    if (flags & (MAP_FIXED | MAP_FIXED_NOREPLACE)) {
        va = (uint64_t)addr;
        if (!user_range(va, size) || va < MMAP_MIN) {
            return -EINVAL;
        }
        if (!vmm_range_is_free(as, va, va + size)) {
            if (flags & MAP_FIXED_NOREPLACE) {
                return -EEXIST;
            }
            vmm_free_range(as, va, va + size);
        }
    } else {
        va = find_free(as, size);
        if (va == 0) {
            return -ENOMEM;
        }
    }

    if (vmm_alloc_range(as, va, va + size, prot_flags(prot)) < 0) {
        vmm_free_range(as, va, va + size);
        return -ENOMEM;
    }
    return (long)va;
}

long sys_munmap(long addr, long len, long a3, long a4, long a5, long a6) {
    (void)a3; (void)a4; (void)a5; (void)a6;
    address_space_t *as = cur_as();
    uint64_t size = align_up_page((uint64_t)len);
    uint64_t end = (uint64_t)addr + size;

    if (len <= 0 || !user_range((uint64_t)addr, size)) {
        return -EINVAL;
    }
    vmm_free_range(as, (uint64_t)addr, end);
    /* Let the next search start above the hole so it can be reused. */
    if (end > as->mmap_hint && (uint64_t)addr < MMAP_TOP) {
        as->mmap_hint = end < MMAP_TOP ? end : MMAP_TOP;
    }
    return 0;
}

long sys_mprotect(long addr, long len, long prot, long a4, long a5, long a6) {
    (void)a4; (void)a5; (void)a6;
    uint64_t size = align_up_page((uint64_t)len);

    if (len <= 0 || !user_range((uint64_t)addr, size)) {
        return -EINVAL;
    }
    vmm_protect_range(cur_as(), (uint64_t)addr, (uint64_t)addr + size, prot_flags(prot));
    return 0;
}
