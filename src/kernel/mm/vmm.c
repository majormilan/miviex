#include <kernel/mm/vmm.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
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
    klog(LOG_DEBUG, "vmm", "PML4 allocated at phys 0x%x", (uint64_t)pml4_phys);

    /*
     * Clone the boot page tables wholesale for now. A later phase should
     * copy only the kernel half of the PML4 and give each process a clean
     * user half: shared low-half tables currently mean PAGE_USER tweaks
     * can leak across address spaces.
     */
    for (int i = 0; i < 512; i++) {
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

void vmm_map_page(address_space_t *as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
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
            klog(LOG_FAIL, "vmm", "Failed to allocate PML3 for virt_addr 0x%x!", virt_addr);
            return;
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
            klog(LOG_FAIL, "vmm", "Failed to allocate PML2 for virt_addr 0x%x!", virt_addr);
            return;
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
             * Split a 2 MiB identity-map huge page into 512 × 4 KiB entries
             * so a single page can get distinct permissions (e.g. PAGE_USER).
             */
            uint64_t huge_phys_base = pml2_entry & ~0x1FFFFFULL;
            uint64_t huge_flags = pml2_entry & (PAGE_PRESENT | PAGE_WRITE);

            phys_addr_t pml1_phys = alloc_zeroed_table();
            if (pml1_phys == PHYS_ADDR_INVALID) {
                klog(LOG_FAIL, "vmm",
                     "Failed to allocate PML1 while splitting huge page for virt_addr 0x%x!",
                     virt_addr);
                return;
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
            klog(LOG_FAIL, "vmm", "Failed to allocate PML1 for virt_addr 0x%x!", virt_addr);
            return;
        }
        pml1 = pt_virt(pml1_phys);
        pml2[pml2_index] =
            pml1_phys | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);
    }

    pml1[pml1_index] = phys_addr | flags;
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void vmm_switch_address_space(address_space_t *as) {
    phys_addr_t pml4_phys = virt_to_phys(as->pml4);
    klog(LOG_DEBUG, "vmm", "Switching address space to PML4 at phys 0x%x", (uint64_t)pml4_phys);
    asm volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

void vmm_destroy_address_space(address_space_t *as) {
    if (as == NULL) {
        return;
    }

    /*
     * Free page-table pages this address space allocated. Entries identical
     * to the shared boot PML4 must not be freed. Huge-page PML2 entries are
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
                pmm_free_page(pml1_phys);
            }

            pmm_free_page(pml2_phys);
        }

        pmm_free_page(pml3_phys);
    }

    pmm_free_page(virt_to_phys(as->pml4));
    k_free(as);
}

void vmm_unmap_page(address_space_t *as, uint64_t virt_addr) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pml3_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pml2_index = (virt_addr >> 21) & 0x1FF;
    uint64_t pml1_index = (virt_addr >> 12) & 0x1FF;

    if (!(as->pml4[pml4_index] & PAGE_PRESENT)) {
        return;
    }
    uint64_t *pml3 = pt_virt(as->pml4[pml4_index] & ~0xFFFULL);

    if (!(pml3[pml3_index] & PAGE_PRESENT)) {
        return;
    }
    uint64_t *pml2 = pt_virt(pml3[pml3_index] & ~0xFFFULL);

    if (!(pml2[pml2_index] & PAGE_PRESENT)) {
        return;
    }
    if (pml2[pml2_index] & PAGE_HUGE) {
        /* Fine-grained unmap of a huge page is not supported yet. */
        return;
    }

    uint64_t *pml1 = pt_virt(pml2[pml2_index] & ~0xFFFULL);
    pml1[pml1_index] = 0;
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}
