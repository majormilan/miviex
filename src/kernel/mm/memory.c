#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>
#include <kernel/mm/pmm.h>

/*  Define the size of the block metadata header */
#define BLOCK_HEADER_SIZE sizeof(mem_block_t)

/*  Initial heap pointer and free list head */
unsigned long *heap_pointer;
mem_block_t *heap_start;
static mem_block_t *free_list = NULL;

/*  Helper function to zero out a memory region */


/*  Helper to find a free block in the free list */
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

/*  Expand the heap when no suitable free block exists */
mem_block_t *k_expand_heap(size_t size) {
    size_t total_size = size + BLOCK_HEADER_SIZE;
    size_t num_pages = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;

    void* first_page = pmm_alloc_page();
    if (!first_page) {
        return NULL; // Out of memory
    }

    // Multi-page blocks need num_pages *physically contiguous* pages. The
    // bitmap PMM always hands out the lowest-numbered free page, so calling
    // it repeatedly back-to-back (no frees in between) normally yields
    // contiguous pages -- but that's an assumption about pmm_alloc_page()'s
    // behaviour, not a guarantee of its interface, so verify it explicitly
    // and bail out cleanly (returning any pages we grabbed) instead of
    // silently claiming -- and later corrupting -- memory we don't own.
    for (size_t i = 1; i < num_pages; i++) {
        void* next_page = pmm_alloc_page();
        bool contiguous = next_page != NULL &&
            (uintptr_t)next_page == (uintptr_t)first_page + i * PAGE_SIZE;
        if (!contiguous) {
            if (next_page) {
                pmm_free_page(next_page);
            }
            for (size_t j = 0; j < i; j++) {
                pmm_free_page((void*)((uintptr_t)first_page + j * PAGE_SIZE));
            }
            return NULL;
        }
    }

    mem_block_t* block = (mem_block_t*)first_page;
    block->size = num_pages * PAGE_SIZE - BLOCK_HEADER_SIZE;
    block->is_free = false;
    block->next = NULL;

    if (free_list == NULL) {
        free_list = block;
    } else {
        mem_block_t* current = free_list;
        while (current->next) {
            current = current->next;
        }
        current->next = block;
    }

    return block;
}

/*  Allocate memory */
void *k_malloc(size_t size) {
  if (size == 0) {
    return NULL;
  }


  /*  Find a free block or expand the heap */
  mem_block_t *block = k_find_free_block(size);
  if (!block) {
    block = k_expand_heap(size);
    if (!block) {
      return NULL; /*  Out of memory */
    }
  } else {
    block->is_free = false;
  }

  /*  Return a pointer to the memory after the block header */
  void *allocated_ptr = (void *)((char *)block + BLOCK_HEADER_SIZE);
  return allocated_ptr;
}

/*  Free memory */
void k_free(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  /*  Get the block header */
  mem_block_t *block_to_free = (mem_block_t *)((char *)ptr - BLOCK_HEADER_SIZE);
  block_to_free->is_free = true;

  // Coalesce with next block
  if (block_to_free->next && block_to_free->next->is_free) {
    block_to_free->size += block_to_free->next->size + BLOCK_HEADER_SIZE;
    block_to_free->next = block_to_free->next->next;
  }

  // Coalesce with previous block
  mem_block_t* current = free_list;
  while(current) {
      if(current->next == block_to_free && current->is_free) {
          current->size += block_to_free->size + BLOCK_HEADER_SIZE;
          current->next = block_to_free->next;
          break;
      }
      current = current->next;
  }
}

/*  Initialize memory */
int k_memory_init(void) {
  pmm_init();
  /*  Initialize the free list as empty */
  free_list = NULL;
  k_heap_init();
  return 0;
}

void k_heap_init() {
    heap_start = (mem_block_t*)pmm_alloc_page(); // Allocate the first page for the heap
    if (!heap_start) {
        // Handle error: cannot allocate initial heap page
        return;
    }
    heap_start->size = PAGE_SIZE - BLOCK_HEADER_SIZE; // Initial size of the block
    heap_start->is_free = true;
    heap_start->next = NULL;
    free_list = heap_start; // Add the initial block to the free list
    heap_pointer = (unsigned long*)((uint64_t)heap_start + PAGE_SIZE); // Point to the end of the first page
}
