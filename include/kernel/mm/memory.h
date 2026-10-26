#ifndef MEMORY_H
#define MEMORY_H

#include <kernel/types.h>
#include <stdint.h>

/* 4 KiB page size: single definition used by PMM, VMM, and the heap. */
#define PAGE_SIZE 0x1000ULL

/* Physical frame address (always a multiple of PAGE_SIZE when valid). */
typedef uint64_t phys_addr_t;

/* Sentinel for “no page”: page 0 is never handed out by the PMM. */
#define PHYS_ADDR_INVALID ((phys_addr_t)0)

/*
 * Temporary identity map helpers.
 *
 * The boot stub identity-maps the low 1 GiB (phys == virt). Until a
 * higher-half direct map (HHDM) exists, these are pure casts. All kernel
 * code that touches a physical frame as a C pointer must go through
 * phys_to_virt() so the HHDM switch is a one-line change later.
 */
static inline void *phys_to_virt(phys_addr_t phys) {
    return (void *)(uintptr_t)phys;
}

static inline phys_addr_t virt_to_phys(const void *virt) {
    return (phys_addr_t)(uintptr_t)virt;
}

static inline phys_addr_t align_down_page(phys_addr_t addr) {
    return addr & ~(PAGE_SIZE - 1);
}

static inline phys_addr_t align_up_page(phys_addr_t addr) {
    return (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

/* Block header for the kernel freelist allocator. */
typedef struct mem_block {
    size_t size;            /* Usable payload bytes (not including this header) */
    bool is_free;
    struct mem_block *next;
} mem_block_t;

extern unsigned long *heap_pointer;
extern mem_block_t *heap_start;

/* Boot page tables (src/boot/main.asm): still identity-mapped. */
extern uint64_t page_table_l4[];
extern uint64_t page_table_l3[];
extern uint64_t page_table_l2[];

void *k_malloc(size_t size);
void k_free(void *ptr);
int k_memory_init(void);
void k_heap_init(void);

mem_block_t *k_find_free_block(size_t size);
mem_block_t *k_expand_heap(size_t size);

#endif /* MEMORY_H */
