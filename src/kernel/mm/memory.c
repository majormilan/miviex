#include <kernel/mm/memory.h>
#include <kernel/mm/pmm.h>
#include <kernel/log.h>
#include <kernel/libc/string.h>

#define BLOCK_HEADER_SIZE sizeof(mem_block_t)
#define ALIGN 16
#define MIN_SPLIT (BLOCK_HEADER_SIZE + ALIGN)

unsigned long *heap_pointer;
mem_block_t *heap_start;
static mem_block_t *free_list = NULL;

/* Blocks are kept in address order; only physical neighbours may merge. */
static bool adjacent(mem_block_t *a, mem_block_t *b) {
    return (char *)a + BLOCK_HEADER_SIZE + a->size == (char *)b;
}

mem_block_t *k_find_free_block(size_t size) {
    mem_block_t *current = free_list;
    while (current) {
        if (current->is_free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

mem_block_t *k_expand_heap(size_t size) {
    size_t total_size = size + BLOCK_HEADER_SIZE;
    size_t num_pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    /*
     * Request a physically contiguous run up front. Once the kernel heap
     * lives in its own virtual region, pages need not be contiguous and
     * this can become a loop of pmm_alloc_page() + vmm_map_page().
     */
    phys_addr_t phys = pmm_alloc_pages(num_pages);
    if (phys == PHYS_ADDR_INVALID) {
        return NULL;
    }

    mem_block_t *block = (mem_block_t *)phys_to_virt(phys);
    block->size = num_pages * PAGE_SIZE - BLOCK_HEADER_SIZE;
    block->is_free = false;
    block->next = NULL;

    if (free_list == NULL || block < free_list) {
        block->next = free_list;
        free_list = block;
    } else {
        mem_block_t *current = free_list;
        while (current->next && current->next < block) {
            current = current->next;
        }
        block->next = current->next;
        current->next = block;
        if (current->is_free && adjacent(current, block)) {
            current->size += block->size + BLOCK_HEADER_SIZE;
            current->next = block->next;
            return current;
        }
    }

    return block;
}

static void split(mem_block_t *block, size_t size) {
    if (block->size < size + MIN_SPLIT) {
        return;
    }
    mem_block_t *rest = (mem_block_t *)((char *)block + BLOCK_HEADER_SIZE + size);
    rest->size = block->size - size - BLOCK_HEADER_SIZE;
    rest->is_free = true;
    rest->next = block->next;
    block->size = size;
    block->next = rest;
}

void *k_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size = (size + ALIGN - 1) & ~(size_t)(ALIGN - 1);

    mem_block_t *block = k_find_free_block(size);
    if (!block) {
        block = k_expand_heap(size);
        if (!block) {
            return NULL;
        }
    }
    block->is_free = false;
    split(block, size);

    return (void *)((char *)block + BLOCK_HEADER_SIZE);
}

void k_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    mem_block_t *block = (mem_block_t *)((char *)ptr - BLOCK_HEADER_SIZE);
    block->is_free = true;

    if (block->next && block->next->is_free && adjacent(block, block->next)) {
        block->size += block->next->size + BLOCK_HEADER_SIZE;
        block->next = block->next->next;
    }

    for (mem_block_t *prev = free_list; prev; prev = prev->next) {
        if (prev->next == block) {
            if (prev->is_free && adjacent(prev, block)) {
                prev->size += block->size + BLOCK_HEADER_SIZE;
                prev->next = block->next;
            }
            break;
        }
    }
}

int k_memory_init(void) {
    pmm_init();
    free_list = NULL;
    k_heap_init();
    return 0;
}

void k_heap_init(void) {
    phys_addr_t phys = pmm_alloc_page();
    if (phys == PHYS_ADDR_INVALID) {
        klog(LOG_FAIL, "heap", "Failed to allocate initial heap page");
        heap_start = NULL;
        free_list = NULL;
        return;
    }

    heap_start = (mem_block_t *)phys_to_virt(phys);
    heap_start->size = PAGE_SIZE - BLOCK_HEADER_SIZE;
    heap_start->is_free = true;
    heap_start->next = NULL;
    free_list = heap_start;
    heap_pointer = (unsigned long *)((uint64_t)heap_start + PAGE_SIZE);
}

void *dma_alloc(size_t size, phys_addr_t *phys) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    phys_addr_t p = pmm_alloc_pages(pages);

    if (p == PHYS_ADDR_INVALID) {
        return NULL;
    }
    memset(phys_to_virt(p), 0, pages * PAGE_SIZE);
    *phys = p;
    return phys_to_virt(p);
}

void dma_free(void *virt, size_t size) {
    pmm_free_pages(virt_to_phys(virt), (size + PAGE_SIZE - 1) / PAGE_SIZE);
}
