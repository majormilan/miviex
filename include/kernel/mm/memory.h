#include <kernel/types.h>
#include <stdint.h>
#ifndef MEMORY_H
#define MEMORY_H

#define PAGE_SIZE 0x1000    /*  4KB page size */
#define HEAP_START 0x200000 /*  Start of heap memory (2MB) */

/*  Define size_t and uintptr_t for kernel-level use */
typedef unsigned long size_t;
typedef unsigned long uintptr_t;

/*  Define NULL */
#define NULL ((void *)0)

/*  Block header for memory allocation */
typedef struct mem_block {
  size_t size;            /*  Size of the block (including header) */
  bool is_free;           /*  Whether the block is free */
  struct mem_block *next; /*  Pointer to the next block */
} mem_block_t;

extern unsigned long *heap_pointer; /*  The current heap pointer */
extern mem_block_t *heap_start;

// Page table structures
extern uint64_t page_table_l4[];
extern uint64_t page_table_l3[];
extern uint64_t page_table_l2[];

/*  Function declarations */
void *k_malloc(size_t size);             /*  Allocate memory */
void k_free(void *ptr);                  /*  Free allocated memory */
int k_memory_init(void);                /*  Initialize the memory subsystem */
void k_heap_init();

/*  Global function declarations */
mem_block_t *k_find_free_block(size_t size); /*  Find a free memory block */
mem_block_t *k_expand_heap(size_t size);     /*  Expand the heap if needed */



#endif /*  MEMORY_H */