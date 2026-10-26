#ifndef VMM_H
#define VMM_H

#include <kernel/types.h>
#include <kernel/mm/memory.h>
#include <stdint.h>

#define PAGE_PRESENT (1 << 0)
#define PAGE_WRITE (1 << 1)
#define PAGE_USER (1 << 2)
#define PAGE_HUGE (1 << 7) // "PS" bit: this PML2 entry maps a 2MiB page directly

// User memory layout, top-down: [USER_END) stack, a gap, mmap regions
// growing down from MMAP_TOP, ..., brk heap growing up from the image.
typedef struct {
    uint64_t* pml4;
    uint64_t brk_start; // first byte past the loaded image (page aligned)
    uint64_t brk;       // current program break (not page aligned)
    uint64_t mmap_hint; // next mmap search starts below this
} address_space_t;

address_space_t* vmm_create_address_space();
void vmm_destroy_address_space(address_space_t* as);
int vmm_map_page(address_space_t* as, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void vmm_switch_address_space(address_space_t* as);

// User half is [0, USER_END); the top page stays unmapped so sysret's RCX
// is always canonical.
#define USER_END 0x00007FFFFFFFF000ULL
#define USER_STACK_SIZE (16 * PAGE_SIZE)
#define MMAP_TOP (USER_END - 0x10000000ULL) // 256 MiB below the stack

// True if every byte of [addr, addr+len) is mapped user-accessible in the
// active address space. Guards kernel copies from user pointers.
bool vmm_user_ok(uint64_t addr, size_t len);

// Physical address behind a user-half va in `as`, or PHYS_ADDR_INVALID.
phys_addr_t vmm_translate(address_space_t* as, uint64_t va);

// Copy into `as` (need not be active) through the direct map. 0 or -1.
int vmm_copy_to(address_space_t* as, uint64_t va, const void *src, size_t n);

// Range helpers for user memory; start/end are rounded out to pages.
// alloc maps zeroed frames, keeping pages already present (adding
// PAGE_WRITE if asked); free unmaps and releases frames; protect rewrites
// flags of present pages; is_free is true if nothing is mapped there.
int vmm_alloc_range(address_space_t* as, uint64_t start, uint64_t end, uint64_t flags);
void vmm_free_range(address_space_t* as, uint64_t start, uint64_t end);
void vmm_protect_range(address_space_t* as, uint64_t start, uint64_t end, uint64_t flags);
bool vmm_range_is_free(address_space_t* as, uint64_t start, uint64_t end);

// Returns the first address space created (by kernel_main's initial
// vmm_create_address_space() call), so other subsystems (e.g. the
// scheduler, when mapping pages for a new user process) can map into it
// without every caller needing to thread an address_space_t* through.
address_space_t* vmm_get_kernel_address_space(void);

#endif // VMM_H
