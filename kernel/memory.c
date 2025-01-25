#include "memory.h"
#include "vga.h"

// Define the size of the block metadata header
#define BLOCK_HEADER_SIZE sizeof(mem_block_t)

// Initial heap pointer and free list head
unsigned long *heap_pointer = (unsigned long *)HEAP_START;
static mem_block_t *free_list = NULL;

// Helper function to zero out a memory region
void memset(void *ptr, char value, size_t size) {
    char *p = (char *)ptr;
    for (size_t i = 0; i < size; i++) {
        p[i] = value;
    }
}

// Helper to find a free block in the free list
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

// Expand the heap when no suitable free block exists
mem_block_t *k_expand_heap(size_t size) {
    // Align size to page boundary
    size_t total_size = size + BLOCK_HEADER_SIZE;
    size_t aligned_size = (total_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    // Create a new block at the current heap pointer
    mem_block_t *block = (mem_block_t *)heap_pointer;
    block->size = aligned_size - BLOCK_HEADER_SIZE; // Exclude header size
    block->is_free = false;
    block->next = NULL;

    // Link this block into the free list if it isn't already
    if (!free_list) {
        free_list = block;
    } else {
        mem_block_t *current = free_list;
        while (current->next) {
            current = current->next;
        }
        current->next = block;
    }

    // Advance the heap pointer
    heap_pointer += aligned_size / sizeof(unsigned long);

    return block;
}

// Allocate memory
void *k_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }

    // Find a free block or expand the heap
    mem_block_t *block = k_find_free_block(size);
    if (!block) {
        block = k_expand_heap(size);
        if (!block) {
            return NULL;  // Out of memory
        }
    } else {
        block->is_free = false;
    }

    // Return a pointer to the memory after the block header
    return (void *)((char *)block + BLOCK_HEADER_SIZE);
}

// Free memory
void k_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    // Get the block header
    mem_block_t *block = (mem_block_t *)((char *)ptr - BLOCK_HEADER_SIZE);
    block->is_free = true;

    // Attempt to coalesce adjacent free blocks
    mem_block_t *current = free_list;
    while (current) {
        if (current->is_free && current->next && current->next->is_free) {
            current->size += current->next->size + BLOCK_HEADER_SIZE;
            current->next = current->next->next;
        }
        current = current->next;
    }
}

// Initialize memory
void k_memory_init(void) {
    terminal_print_colorful("Memory subsystem initialized\n", VGA_COLOR_LIGHT_BLUE);

    // Initialize the free list as empty
    free_list = NULL;

    // Print heap start address
    char buf[50];
    itoa((uintptr_t)HEAP_START, buf, 16);
    terminal_print("Heap starts at: ");
    terminal_print(buf);
    terminal_print("\n");
}

void itoa(int num, char *str, int base) {
    int i = 0;
    int is_negative = 0;

    // Handle 0 explicitly
    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    // Handle negative numbers (only if base is 10)
    if (num < 0 && base == 10) {
        is_negative = 1;
        num = -num;
    }

    // Process digits
    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num /= base;
    }

    // Add negative sign if applicable
    if (is_negative) {
        str[i++] = '-';
    }

    str[i] = '\0';  // Null-terminate the string

    // Reverse the string
    int start = 0, end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}
