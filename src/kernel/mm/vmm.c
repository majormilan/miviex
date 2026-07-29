#include <kernel/mm/vmm.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/memory.h>
#include <kernel/log.h>
#include <stddef.h>

extern uint64_t page_table_l4[];

// Kernel section symbols from linker script
extern uint8_t _text_start[];
extern uint8_t _text_end[];
extern uint8_t _rodata_start[];
extern uint8_t _rodata_end[];
extern uint8_t _data_start[];
extern uint8_t _data_end[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static address_space_t* kernel_address_space = NULL;

address_space_t* vmm_create_address_space() {
    klog(LOG_DEBUG, "vmm", "Creating new address space...");
    address_space_t* as = k_malloc(sizeof(address_space_t));
    if (!as) {
        klog(LOG_FAIL, "vmm", "Failed to allocate address space structure!");
        return NULL;
    }

    as->pml4 = pmm_alloc_page();
    if (!as->pml4) {
        klog(LOG_FAIL, "vmm", "Failed to allocate PML4!");
        k_free(as);
        return NULL;
    }
    klog(LOG_DEBUG, "vmm", "PML4 allocated at 0x%x", (uint64_t)as->pml4);

    // Copy kernel page tables
    for (int i = 0; i < 512; i++) {
        as->pml4[i] = page_table_l4[i];
    }

    if (kernel_address_space == NULL) {
        kernel_address_space = as;
    }

    return as;
}

address_space_t* vmm_get_kernel_address_space(void) {
    return kernel_address_space;
}

void vmm_map_page(address_space_t* as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pml3_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pml2_index = (virt_addr >> 21) & 0x1FF;
    uint64_t pml1_index = (virt_addr >> 12) & 0x1FF;

    bool want_user = (flags & PAGE_USER) != 0;

    uint64_t* pml3 = NULL;
    if (as->pml4[pml4_index] & PAGE_PRESENT) {
        pml3 = (uint64_t*)(as->pml4[pml4_index] & ~0xFFFULL);
        // A directory entry's U bit must be set for anything beneath it to
        // be reachable from user mode at all, even if it was originally
        // created only for kernel mappings (e.g. the shared boot-time
        // identity map that every address space clones).
        if (want_user) {
            as->pml4[pml4_index] |= PAGE_USER;
        }
    } else {
        pml3 = pmm_alloc_page();
        if (!pml3) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML3 for virt_addr 0x%x!", virt_addr);
            return;
        }
        for (int i = 0; i < 512; i++) pml3[i] = 0;
        as->pml4[pml4_index] = (uint64_t)pml3 | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);
    }

    uint64_t* pml2 = NULL;
    if (pml3[pml3_index] & PAGE_PRESENT) {
        pml2 = (uint64_t*)(pml3[pml3_index] & ~0xFFFULL);
        if (want_user) {
            pml3[pml3_index] |= PAGE_USER;
        }
    } else {
        pml2 = pmm_alloc_page();
        if (!pml2) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML2 for virt_addr 0x%x!", virt_addr);
            return;
        }
        for (int i = 0; i < 512; i++) pml2[i] = 0;
        pml3[pml3_index] = (uint64_t)pml2 | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);
    }

    uint64_t* pml1 = NULL;
    uint64_t pml2_entry = pml2[pml2_index];
    if (pml2_entry & PAGE_PRESENT) {
        if (pml2_entry & PAGE_HUGE) {
            // This 2MiB region (e.g. part of the boot-time identity map) is
            // currently a single huge page. Split it into a normal 4KiB
            // PML1 table -- replicating the original mapping across all
            // 512 sub-pages -- so we can grant fine-grained permissions
            // (like PAGE_USER) to just the one page being mapped here,
            // without disturbing the other 511.
            uint64_t huge_phys_base = pml2_entry & ~0x1FFFFFULL; // 2MiB aligned
            uint64_t huge_flags = pml2_entry & (PAGE_PRESENT | PAGE_WRITE);

            pml1 = pmm_alloc_page();
            if (!pml1) {
                klog(LOG_FAIL, "vmm", "Failed to allocate PML1 while splitting huge page for virt_addr 0x%x!", virt_addr);
                return;
            }
            for (uint64_t i = 0; i < 512; i++) {
                pml1[i] = (huge_phys_base + i * PAGE_SIZE) | huge_flags;
            }
            pml2[pml2_index] = (uint64_t)pml1 | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);

            // The TLB may still cache the old huge-page translation for any
            // address in this 2MiB range; flush all of it now that it maps
            // through a real page table instead.
            uint64_t region_base = virt_addr & ~0x1FFFFFULL;
            for (uint64_t i = 0; i < 512; i++) {
                asm volatile("invlpg (%0)" : : "r"(region_base + i * PAGE_SIZE) : "memory");
            }
        } else {
            pml1 = (uint64_t*)(pml2_entry & ~0xFFFULL);
            if (want_user) {
                pml2[pml2_index] |= PAGE_USER;
            }
        }
    } else {
        pml1 = pmm_alloc_page();
        if (!pml1) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML1 for virt_addr 0x%x!", virt_addr);
            return;
        }
        for (int i = 0; i < 512; i++) pml1[i] = 0;
        pml2[pml2_index] = (uint64_t)pml1 | PAGE_PRESENT | PAGE_WRITE | (want_user ? PAGE_USER : 0);
    }

    pml1[pml1_index] = phys_addr | flags;
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}

void vmm_switch_address_space(address_space_t* as) {
    klog(LOG_DEBUG, "vmm", "Switching address space to PML4 at 0x%x", (uint64_t)as->pml4);
    asm volatile("mov %0, %%cr3" : : "r"(as->pml4));
}

void vmm_destroy_address_space(address_space_t* as) {
    if (as == NULL) {
        return;
    }

    // Free every page-table structure this address space allocated for
    // itself, but leave alone anything inherited from the shared kernel
    // page tables set up in vmm_create_address_space() (identical entries
    // in page_table_l4 must not be freed, since other address spaces still
    // reference them).
    for (int pml4_idx = 0; pml4_idx < 512; pml4_idx++) {
        uint64_t pml4_entry = as->pml4[pml4_idx];
        if (!(pml4_entry & PAGE_PRESENT) || pml4_entry == page_table_l4[pml4_idx]) {
            continue;
        }

        uint64_t* pml3 = (uint64_t*)(pml4_entry & ~0xFFFULL);
        for (int pml3_idx = 0; pml3_idx < 512; pml3_idx++) {
            uint64_t pml3_entry = pml3[pml3_idx];
            if (!(pml3_entry & PAGE_PRESENT)) {
                continue;
            }

            uint64_t* pml2 = (uint64_t*)(pml3_entry & ~0xFFFULL);
            for (int pml2_idx = 0; pml2_idx < 512; pml2_idx++) {
                uint64_t pml2_entry = pml2[pml2_idx];
                if (!(pml2_entry & PAGE_PRESENT)) {
                    continue;
                }

                uint64_t* pml1 = (uint64_t*)(pml2_entry & ~0xFFFULL);
                pmm_free_page(pml1);
            }

            pmm_free_page(pml2);
        }

        pmm_free_page(pml3);
    }

    pmm_free_page(as->pml4);
    k_free(as);
}

void vmm_unmap_page(address_space_t* as, uint64_t virt_addr) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pml3_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pml2_index = (virt_addr >> 21) & 0x1FF;
    uint64_t pml1_index = (virt_addr >> 12) & 0x1FF;

    if (!(as->pml4[pml4_index] & PAGE_PRESENT)) {
        return; // Not mapped
    }
    uint64_t* pml3 = (uint64_t*)(as->pml4[pml4_index] & ~0xFFFULL);

    if (!(pml3[pml3_index] & PAGE_PRESENT)) {
        return;
    }
    uint64_t* pml2 = (uint64_t*)(pml3[pml3_index] & ~0xFFFULL);

    if (!(pml2[pml2_index] & PAGE_PRESENT)) {
        return;
    }
    uint64_t* pml1 = (uint64_t*)(pml2[pml2_index] & ~0xFFFULL);

    pml1[pml1_index] = 0;

    // Flush any stale TLB entry for this virtual address.
    asm volatile("invlpg (%0)" : : "r"(virt_addr) : "memory");
}
