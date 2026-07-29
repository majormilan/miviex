#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define PAGE_PRESENT (1 << 0)
#define PAGE_WRITE (1 << 1)
#define PAGE_USER (1 << 2)
#define PAGE_HUGE (1 << 7) // "PS" bit: this PML2 entry maps a 2MiB page directly

typedef struct {
    uint64_t* pml4;
} address_space_t;

address_space_t* vmm_create_address_space();
void vmm_destroy_address_space(address_space_t* as);
void vmm_map_page(address_space_t* as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void vmm_unmap_page(address_space_t* as, uint64_t virt_addr);
void vmm_switch_address_space(address_space_t* as);

// Returns the first address space created (by kernel_main's initial
// vmm_create_address_space() call), so other subsystems (e.g. the
// scheduler, when mapping pages for a new user process) can map into it
// without every caller needing to thread an address_space_t* through.
address_space_t* vmm_get_kernel_address_space(void);

#endif // VMM_H
