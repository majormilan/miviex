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

    return as;
}

void vmm_map_page(address_space_t* as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags) {
    uint64_t pml4_index = (virt_addr >> 39) & 0x1FF;
    uint64_t pml3_index = (virt_addr >> 30) & 0x1FF;
    uint64_t pml2_index = (virt_addr >> 21) & 0x1FF;
    uint64_t pml1_index = (virt_addr >> 12) & 0x1FF;

    uint64_t* pml3 = NULL;
    if (as->pml4[pml4_index] & PAGE_PRESENT) {
        pml3 = (uint64_t*)(as->pml4[pml4_index] & ~0xFFF);
    } else {
        pml3 = pmm_alloc_page();
        if (!pml3) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML3 for virt_addr 0x%x!", virt_addr);
            return;
        }
        for (int i = 0; i < 512; i++) pml3[i] = 0;
        as->pml4[pml4_index] = (uint64_t)pml3 | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint64_t* pml2 = NULL;
    if (pml3[pml3_index] & PAGE_PRESENT) {
        pml2 = (uint64_t*)(pml3[pml3_index] & ~0xFFF);
    } else {
        pml2 = pmm_alloc_page();
        if (!pml2) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML2 for virt_addr 0x%x!", virt_addr);
            return;
        }
        for (int i = 0; i < 512; i++) pml2[i] = 0;
        pml3[pml3_index] = (uint64_t)pml2 | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint64_t* pml1 = NULL;
    if (pml2[pml2_index] & PAGE_PRESENT) {
        pml1 = (uint64_t*)(pml2[pml2_index] & ~0xFFF);
    } else {
        pml1 = pmm_alloc_page();
        if (!pml1) {
            klog(LOG_FAIL, "vmm", "Failed to allocate PML1 for virt_addr 0x%x!", virt_addr);
            return;
        }
        for (int i = 0; i < 512; i++) pml1[i] = 0;
        pml2[pml2_index] = (uint64_t)pml1 | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    pml1[pml1_index] = phys_addr | flags;
}

void vmm_switch_address_space(address_space_t* as) {
    klog(LOG_DEBUG, "vmm", "Switching address space to PML4 at 0x%x", (uint64_t)as->pml4);
    asm volatile("mov %0, %%cr3" : : "r"(as->pml4));
}

void vmm_destroy_address_space(address_space_t* as) {
    // TODO: Implement this
}

void vmm_unmap_page(address_space_t* as, uint64_t virt_addr) {
    // TODO: Implement this
}
