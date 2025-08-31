#include <kernel/mm/vmm.h>
#include <kernel/mm/pmm.h>
#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>
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
    terminal_print_colorful("VMM: Creating address space...\n", VGA_COLOR_LIGHT_CYAN);
    address_space_t* as = k_malloc(sizeof(address_space_t));
    if (!as) {
        terminal_print_colorful("VMM: Failed to allocate address space structure!\n", VGA_COLOR_LIGHT_RED);
        return NULL;
    }

    terminal_print_colorful("VMM: Allocating PML4...\n", VGA_COLOR_LIGHT_CYAN);
    as->pml4 = pmm_alloc_page();
    if (!as->pml4) {
        terminal_print_colorful("VMM: Failed to allocate PML4!\n", VGA_COLOR_LIGHT_RED);
        k_free(as);
        return NULL;
    }
    terminal_print_colorful("VMM: PML4 allocated at ", VGA_COLOR_LIGHT_CYAN);
    terminal_print_hex((uint64_t)as->pml4);
    terminal_print_colorful("\n", VGA_COLOR_LIGHT_CYAN);

    // Copy kernel page tables (identity mapping for lower half)
    terminal_print_colorful("VMM: Copying kernel page tables...\n", VGA_COLOR_LIGHT_CYAN);
    for (int i = 0; i < 256; i++) { // Copy lower half (identity mapped)
        as->pml4[i] = page_table_l4[i];
    }
    for (int i = 256; i < 512; i++) { // Copy higher half (kernel mapped)
        as->pml4[i] = page_table_l4[i];
    }
    terminal_print_colorful("VMM: Kernel page tables copied.\n", VGA_COLOR_LIGHT_CYAN);

    // Map kernel sections
    terminal_print_colorful("VMM: Mapping kernel sections...\n", VGA_COLOR_LIGHT_CYAN);
    for (uint64_t addr = (uint64_t)_text_start; addr < (uint64_t)_text_end; addr += PAGE_SIZE) {
        vmm_map_page(as, addr, addr, PAGE_PRESENT | PAGE_WRITE);
    }
    for (uint64_t addr = (uint64_t)_rodata_start; addr < (uint64_t)_rodata_end; addr += PAGE_SIZE) {
        vmm_map_page(as, addr, addr, PAGE_PRESENT);
    }
    for (uint64_t addr = (uint64_t)_data_start; addr < (uint64_t)_data_end; addr += PAGE_SIZE) {
        vmm_map_page(as, addr, addr, PAGE_PRESENT | PAGE_WRITE);
    }
    for (uint64_t addr = (uint64_t)__bss_start; addr < (uint64_t)__bss_end; addr += PAGE_SIZE) {
        vmm_map_page(as, addr, addr, PAGE_PRESENT | PAGE_WRITE);
    }
    terminal_print_colorful("VMM: Kernel sections mapped.\n", VGA_COLOR_LIGHT_CYAN);

    // Map VGA buffer
    terminal_print_colorful("VMM: Mapping VGA buffer...\n", VGA_COLOR_LIGHT_CYAN);
    vmm_map_page(as, 0xB8000, 0xB8000, PAGE_PRESENT | PAGE_WRITE);
    terminal_print_colorful("VMM: VGA buffer mapped.\n", VGA_COLOR_LIGHT_CYAN);

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
            terminal_print_colorful("VMM: Failed to allocate PML3!\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        // Clear the new page table
        for (int i = 0; i < 512; i++) pml3[i] = 0;
        as->pml4[pml4_index] = (uint64_t)pml3 | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint64_t* pml2 = NULL;
    if (pml3[pml3_index] & PAGE_PRESENT) {
        pml2 = (uint64_t*)(pml3[pml3_index] & ~0xFFF);
    } else {
        pml2 = pmm_alloc_page();
        if (!pml2) {
            terminal_print_colorful("VMM: Failed to allocate PML2!\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        // Clear the new page table
        for (int i = 0; i < 512; i++) pml2[i] = 0;
        pml3[pml3_index] = (uint64_t)pml2 | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint64_t* pml1 = NULL;
    if (pml2[pml2_index] & PAGE_PRESENT) {
        pml1 = (uint64_t*)(pml2[pml2_index] & ~0xFFF);
    } else {
        pml1 = pmm_alloc_page();
        if (!pml1) {
            terminal_print_colorful("VMM: Failed to allocate PML1!\n", VGA_COLOR_LIGHT_RED);
            return;
        }
        // Clear the new page table
        for (int i = 0; i < 512; i++) pml1[i] = 0;
        pml2[pml2_index] = (uint64_t)pml1 | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    pml1[pml1_index] = phys_addr | flags;
}

void vmm_switch_address_space(address_space_t* as) {
    terminal_print_colorful("VMM: Switching address space to ", VGA_COLOR_LIGHT_CYAN);
    terminal_print_hex((uint64_t)as->pml4);
    terminal_print_colorful("\n", VGA_COLOR_LIGHT_CYAN);
    asm volatile("mov %0, %%cr3" : : "r"(as->pml4));
    terminal_print_colorful("VMM: Address space switched.\n", VGA_COLOR_LIGHT_CYAN);
}

void vmm_destroy_address_space(address_space_t* as) {
    // TODO: Implement this
}

void vmm_unmap_page(address_space_t* as, uint64_t virt_addr) {
    // TODO: Implement this
}
