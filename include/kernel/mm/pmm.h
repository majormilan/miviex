#ifndef PMM_H
#define PMM_H

#include <kernel/mm/memory.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Physical memory manager.
 *
 * Tracks frames with a bitmap sized from the Multiboot memory map, placed
 * in the first AVAILABLE region clear of the kernel, the Multiboot info
 * and the boot modules. Only frames inside the kernel's direct-map window
 * (low 1 GiB at KERNEL_BASE) are allocatable for now; RAM above that is
 * reported but left alone until a larger direct map exists.
 */

/* Must match the boot stub in src/boot/main.asm (512 × 2 MiB huge pages). */
#define PMM_DIRECT_MAP_END 0x40000000ULL /* 1 GiB */

typedef struct {
    uint64_t total_frames;     /* Frames covered by the bitmap */
    uint64_t used_frames;      /* Currently allocated or reserved */
    uint64_t free_frames;      /* Available for pmm_alloc_* */
    uint64_t usable_bytes;     /* Free frames × PAGE_SIZE at init (approx) */
    uint64_t detected_ram;     /* Sum of Multiboot AVAILABLE lengths */
    uint64_t managed_bytes;    /* Bytes under PMM control (<= direct map) */
} pmm_stats_t;

void pmm_init(void);

/* Allocate one frame. Returns PHYS_ADDR_INVALID on failure. */
phys_addr_t pmm_alloc_page(void);

/* Allocate `count` contiguous frames. Returns base phys or PHYS_ADDR_INVALID. */
phys_addr_t pmm_alloc_pages(size_t count);

/* Free one frame previously returned by pmm_alloc_page/pages. */
void pmm_free_page(phys_addr_t page);

/* Free `count` contiguous frames starting at `page`. */
void pmm_free_pages(phys_addr_t page, size_t count);

void pmm_get_stats(pmm_stats_t *out);

#endif /* PMM_H */
