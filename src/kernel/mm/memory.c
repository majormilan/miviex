#include <kernel/mm/memory.h>
#include <kernel/video/vga.h>

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

void k_heap_init() {
    heap_start = (mem_block_t*)HEAP_START;
    heap_start->size = 0; // Initially no memory in heap
    heap_start->is_free = true;
    heap_start->next = NULL;
    heap_pointer = (unsigned long*)HEAP_START;
}

uint64_t detect_memory_size() {
    uint64_t total_memory = 0;
    uint64_t address = 0;
    // Probe memory in 4KB chunks
    while (address < 0xFFFFFFFFF) { // Probe up to 4GB for now
        volatile uint32_t *ptr = (volatile uint32_t *)address;
        uint32_t original_value = *ptr;
        *ptr = 0xDEADBEEF; // Write a test pattern
        if (*ptr == 0xDEADBEEF) {
            *ptr = original_value; // Restore original value
            total_memory += 4096; // Add 4KB if memory is present
        } else {
            // Memory not present or not writable
            break;
        }
        address += 4096;
    }
    return total_memory;
}