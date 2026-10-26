#include <kernel/mm/vmm.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <kernel/libc/string.h>
#include <stddef.h>

extern uint64_t page_table_l4[];

extern uint8_t _text_start[];
extern uint8_t _text_end[];
extern uint8_t _rodata_start[];
extern uint8_t _rodata_end[];
extern uint8_t _data_start[];
extern uint8_t _data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

#define ADDR_MASK 0x000FFFFFFFFFF000ULL

static address_space_t *kernel_address_space = NULL;

static uint64_t *pt_virt(phys_addr_t table_phys) {
    return (uint64_t *)phys_to_virt(table_phys);
}

static phys_addr_t alloc_zeroed_table(void) {
    phys_addr_t phys = pmm_alloc_page();
    if (phys == PHYS_ADDR_INVALID) {
        return PHYS_ADDR_INVALID;
    }
    uint64_t *table = pt_virt(phys);
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }
    return phys;
}

address_space_t *vmm_create_address_space(void) {
    klog(LOG_DEBUG, "vmm", "Creating new address space...");
    address_space_t *as = k_malloc(sizeof(address_space_t));
    if (!as) {
        klog(LOG_FAIL, "vmm", "Failed to allocate address space structure!");
        return NULL;
    }

    phys_addr_t pml4_phys = pmm_alloc_page();
    if (pml4_phys == PHYS_ADDR_INVALID) {
        klog(LOG_FAIL, "vmm", "Failed to allocate PML4!");
        k_free(as);
        return NULL;
    }

    as->pml4 = pt_virt(pml4_phys);
    as->brk_start = 0;
    as->brk = 0;
    as->mmap_hint = MMAP_TOP;
    klog(LOG_DEBUG, "vmm", "PML4 allocated at phys 0x%lx", pml4_phys);

    /*
     * Share the kernel half (PML4 256..511) with the boot tables; the user
     * half starts empty. Kernel-half tables are never copied, so all
     * address spaces see the same kernel mappings.
     */
    for (int i = 0; i < 256; i++) {
        as->pml4[i] = 0;
    }
    for (int i = 256; i < 512; i++) {
        as->pml4[i] = page_table_l4[i];
    }

    if (kernel_address_space == NULL) {
        kernel_address_space = as;
    }

    return as;
}

address_space_t *vmm_get_kernel_address_space(void) {
    return kernel_address_space;
}

int vmm_map_page(address_space_t *as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pml3_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pml2_index = (virt_addr >> 21) & 0x1FF;
    uint64_t pml1_index = (virt_addr >> 12) & 0x1FF;

    bool want_user = (flags & PAGE_USER) != 0;

    uint64_t *pml3 = NULL;
    if (as->pml4[pml4_index] & PAGE_PRESENT) {
        pml3 = pt_virt(as->pml4[pml4_index] & ~0xFFFULL);
        if (want_user) {
            as->pml4[pml4_index] |= PAGE_USER;
        }
    } else {
        phys_addr_t pml3_phys = alloc_zeroed_table();
        if (pml3_phys == PHYS_ADDR_INVALID) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML3 for virt_addr 0x%lx!", virt_addr);
            return -1;
        }
        pml3 = pt_virt(pml3_phys);
        as->pml4[pml4_index] =
            pml3_phys | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);
    }

    uint64_t *pml2 = NULL;
    if (pml3[pml3_index] & PAGE_PRESENT) {
        pml2 = pt_virt(pml3[pml3_index] & ~0xFFFULL);
        if (want_user) {
            pml3[pml3_index] |= PAGE_USER;
        }
    } else {
        phys_addr_t pml2_phys = alloc_zeroed_table();
        if (pml2_phys == PHYS_ADDR_INVALID) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML2 for virt_addr 0x%lx!", virt_addr);
            return -1;
        }
        pml2 = pt_virt(pml2_phys);
        pml3[pml3_index] =
            pml2_phys | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);
    }

    uint64_t *pml1 = NULL;
    uint64_t pml2_entry = pml2[pml2_index];
    if (pml2_entry & PAGE_PRESENT) {
        if (pml2_entry & PAGE_HUGE) {
            /*
             * Split a 2 MiB direct-map huge page into 512 × 4 KiB entries
             * so a single page can get distinct permissions (e.g. PAGE_USER).
             */
            uint64_t huge_phys_base = pml2_entry & ~0x1FFFFFULL;
            uint64_t huge_flags = pml2_entry & (PAGE_PRESENT | PAGE_WRITE);

            phys_addr_t pml1_phys = alloc_zeroed_table();
            if (pml1_phys == PHYS_ADDR_INVALID) {
                klog(LOG_FAIL, "vmm",
                     "Failed to allocate PML1 while splitting huge page for virt_addr 0x%lx!",
                     virt_addr);
                return -1;
            }
            pml1 = pt_virt(pml1_phys);
            for (uint64_t i = 0; i < 512; i++) {
                pml1[i] = (huge_phys_base + i * PAGE_SIZE) | huge_flags;
            }
            pml2[pml2_index] =
                pml1_phys | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);

            uint64_t region_base = virt_addr & ~0x1FFFFFULL;
            for (uint64_t i = 0; i < 512; i++) {
                asm volatile("invlpg (%0)" : : "r"(region_base + i * PAGE_SIZE) : "memory");
            }
        } else {
            pml1 = pt_virt(pml2_entry & ~0xFFFULL);
            if (want_user) {
                pml2[pml2_index] |= PAGE_USER;
            }
        }
    } else {
        phys_addr_t pml1_phys = alloc_zeroed_table();
        if (pml1_phys == PHYS_ADDR_INVALID) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML1 for virt_addr 0x%lx!", virt_addr);
            return -1;
        }
        pml1 = pt_virt(pml1_phys);
        pml2[pml2_index] =
            pml1_phys | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);
    }

    pml1[pml1_index] = phys_addr | flags;
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
    return 0;
}

void vmm_switch_address_space(address_space_t *as) {
    asm volatile("mov %0, %%cr3" : : "r"(virt_to_phys(as->pml4)) : "memory");
}

void vmm_destroy_address_space(address_space_t *as) {
    if (as == NULL) {
        return;
    }

    /*
     * Free page-table pages this address space allocated, plus the frames
     * mapped in the user half. Entries identical to the shared boot PML4
     * must not be freed. Huge-page PML2 entries are
     * physical RAM mappings, not table pointers: skip them.
     */
    for (int pml4_idx = 0; pml4_idx < 512; pml4_idx++) {
        uint64_t pml4_entry = as->pml4[pml4_idx];
        /* Compare addresses only: map_page may have OR'ed PAGE_USER in. */
        if (!(pml4_entry & PAGE_PRESENT) ||
            (pml4_entry & ~0xFFFULL) == (page_table_l4[pml4_idx] & ~0xFFFULL)) {
            continue;
        }

        phys_addr_t pml3_phys = pml4_entry & ~0xFFFULL;
        uint64_t *pml3 = pt_virt(pml3_phys);

        for (int pml3_idx = 0; pml3_idx < 512; pml3_idx++) {
            uint64_t pml3_entry = pml3[pml3_idx];
            if (!(pml3_entry & PAGE_PRESENT)) {
                continue;
            }

            phys_addr_t pml2_phys = pml3_entry & ~0xFFFULL;
            uint64_t *pml2 = pt_virt(pml2_phys);

            for (int pml2_idx = 0; pml2_idx < 512; pml2_idx++) {
                uint64_t pml2_entry = pml2[pml2_idx];
                if (!(pml2_entry & PAGE_PRESENT)) {
                    continue;
                }
                if (pml2_entry & PAGE_HUGE) {
                    continue; /* 2 MiB mapping, not a table we allocated */
                }

                phys_addr_t pml1_phys = pml2_entry & ~0xFFFULL;
                if (pml4_idx < 256) {
                    /* User half: the mapped frames belong to this space. */
                    uint64_t *pml1 = pt_virt(pml1_phys);
                    for (int i = 0; i < 512; i++) {
                        if (pml1[i] & PAGE_PRESENT) {
                            pmm_free_page(pml1[i] & ADDR_MASK);
                        }
                    }
                }
                pmm_free_page(pml1_phys);
            }

            pmm_free_page(pml2_phys);
        }

        pmm_free_page(pml3_phys);
    }

    pmm_free_page(virt_to_phys(as->pml4));
    k_free(as);
}

/* Leaf entry for a 4 KiB user-half mapping, or NULL if no table exists. */
static uint64_t *pte(uint64_t *pml4, uint64_t va) {
    uint64_t *t = pml4;

    for (int shift = 39; shift > 12; shift -= 9) {
        uint64_t e = t[(va >> shift) & 0x1FF];
        if (!(e & PAGE_PRESENT) || (e & PAGE_HUGE)) {
            return NULL;
        }
        t = pt_virt(e & ADDR_MASK);
    }
    return &t[(va >> 12) & 0x1FF];
}

phys_addr_t vmm_translate(address_space_t *as, uint64_t va) {
    uint64_t *e = pte(as->pml4, va);

    if (e == NULL || !(*e & PAGE_PRESENT)) {
        return PHYS_ADDR_INVALID;
    }
    return (*e & ADDR_MASK) | (va & (PAGE_SIZE - 1));
}

int vmm_copy_to(address_space_t *as, uint64_t va, const void *src, size_t n) {
    const uint8_t *p = src;

    while (n > 0) {
        size_t off = va & (PAGE_SIZE - 1);
        size_t chunk = PAGE_SIZE - off < n ? PAGE_SIZE - off : n;
        phys_addr_t phys = vmm_translate(as, va);

        if (phys == PHYS_ADDR_INVALID) {
            return -1;
        }
        memcpy(phys_to_virt(phys), p, chunk);
        va += chunk;
        p += chunk;
        n -= chunk;
    }
    return 0;
}

bool vmm_user_ok(uint64_t addr, size_t len, bool write) {
    uint64_t end = addr + len;
    uint64_t need = PAGE_PRESENT | PAGE_USER | (write ? PAGE_WRITE : 0);
    uint64_t cr3;

    if (len == 0) {
        return true;
    }
    if (end < addr || end > USER_END) {
        return false;
    }

    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    for (uint64_t va = addr & ~(PAGE_SIZE - 1); va < end; va += PAGE_SIZE) {
        uint64_t *e = pte(pt_virt(cr3 & ADDR_MASK), va);
        if (e == NULL || (*e & need) != need) {
            return false;
        }
    }
    return true;
}

int vmm_alloc_range(address_space_t *as, uint64_t start, uint64_t end, uint64_t flags) {
    for (uint64_t va = align_down_page(start); va < end; va += PAGE_SIZE) {
        phys_addr_t phys = vmm_translate(as, va);

        if (phys == PHYS_ADDR_INVALID) {
            phys = pmm_alloc_page();
            if (phys == PHYS_ADDR_INVALID) {
                return -1;
            }
            memset(phys_to_virt(phys), 0, PAGE_SIZE);
        } else if (!(flags & PAGE_WRITE)) {
            continue;
        }
        if (vmm_map_page(as, va, phys, flags) < 0) {
            return -1;
        }
    }
    return 0;
}

void vmm_free_range(address_space_t *as, uint64_t start, uint64_t end) {
    for (uint64_t va = align_down_page(start); va < end; va += PAGE_SIZE) {
        uint64_t *e = pte(as->pml4, va);

        if (e != NULL && (*e & PAGE_PRESENT)) {
            pmm_free_page(*e & ADDR_MASK);
            *e = 0;
            asm volatile("invlpg (%0)" : : "r"(va) : "memory");
        }
    }
}

void vmm_protect_range(address_space_t *as, uint64_t start, uint64_t end, uint64_t flags) {
    for (uint64_t va = align_down_page(start); va < end; va += PAGE_SIZE) {
        uint64_t *e = pte(as->pml4, va);

        if (e != NULL && (*e & PAGE_PRESENT)) {
            *e = (*e & ADDR_MASK) | flags;
            asm volatile("invlpg (%0)" : : "r"(va) : "memory");
        }
    }
}

bool vmm_range_is_free(address_space_t *as, uint64_t start, uint64_t end) {
    for (uint64_t va = align_down_page(start); va < end; va += PAGE_SIZE) {
        if (vmm_translate(as, va) != PHYS_ADDR_INVALID) {
            return false;
        }
    }
    return true;
}
