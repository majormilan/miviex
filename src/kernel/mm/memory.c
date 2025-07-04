#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>

/*  Define the size of the block metadata header */
#define BLOCK_HEADER_SIZE sizeof(mem_block_t)

/*  Initial heap pointer and free list head */
unsigned long *heap_pointer = (unsigned long *)HEAP_START;
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
  /*  Align size to page boundary */
  size_t total_size = size + BLOCK_HEADER_SIZE;
  size_t aligned_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  /*  Create a new block at the current heap pointer */
  mem_block_t *block = (mem_block_t *)heap_pointer;
  block->size = aligned_size - BLOCK_HEADER_SIZE; /*  Exclude header size */
  block->is_free = false;
  block->next = NULL;

  /*  Link this block into the free list if it isn't already */
  if (!free_list) {
    free_list = block;
  } else {
    mem_block_t *current = free_list;
    while (current->next) {
      current = current->next;
    }
    current->next = block;
  }

  /*  Advance the heap pointer */
  heap_pointer += aligned_size / sizeof(unsigned long);

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
  mem_block_t *block = (mem_block_t *)((char *)ptr - BLOCK_HEADER_SIZE);
  block->is_free = true;

  /*  Attempt to coalesce adjacent free blocks */
  mem_block_t *current = free_list;
  while (current) {
    if (current->is_free && current->next && current->next->is_free) {
      current->size += current->next->size + BLOCK_HEADER_SIZE;
      current->next = current->next->next;
    }
    current = current->next;
  }
}

/*  Initialize memory */
void k_memory_init(void) {

  /*  Initialize the free list as empty */
  free_list = NULL;
  k_heap_init();
}

void k_heap_init(void) {    terminal_print_colorful("Initializing heap...\n", VGA_COLOR_LIGHT_BLUE);    // Map the first 4MB of memory for the heap    for (uintptr_t i = HEAP_START; i < HEAP_START + (4 * 1024 * 1024); i += PAGE_SIZE) {        terminal_print("Mapping page: ");        terminal_print_num(i);        terminal_print("\n");        map_page(i, i, 0x3);    }    terminal_print_colorful("Heap initialized.\n", VGA_COLOR_LIGHT_BLUE);
}

// Function to map a virtual address to a physical address
void map_page(uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags) {
    // Get the page table indices
    uint64_t pml4_index = (virtual_address >> 39) & 0x1FF;
    uint64_t pdpt_index = (virtual_address >> 30) & 0x1FF;
    uint64_t pd_index = (virtual_address >> 21) & 0x1FF;

    // Get the PML4 entry
    uint64_t *pml4_entry = &page_table_l4[pml4_index];

    // If the PDPT is not present, create it
    if (!(*pml4_entry & 0x1)) {
        *pml4_entry = (uint64_t)page_table_l3 | 0x3; // Present, Writable
    }

    // Get the PDPT entry
    uint64_t *pdpt_entry = &page_table_l3[pdpt_index];

    // If the PD is not present, create it
    if (!(*pdpt_entry & 0x1)) {
        *pdpt_entry = (uint64_t)page_table_l2 | 0x3; // Present, Writable
    }

    // Get the PD entry and map the page
    uint64_t *pd_entry = &page_table_l2[pd_index];
    *pd_entry = physical_address | flags;
}






