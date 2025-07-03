#include <kernel/types.h>
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

// Page table structures
extern uint64_t page_table_l4[];
extern uint64_t page_table_l3[];
extern uint64_t page_table_l2[];

/*  Function declarations */
void itoa(uintptr_t num, char *str, int base); /*  Integer to ASCII conversion */
char* strcpy(char* dest, const char* src);
int strcmp(const char *s1, const char *s2);
void *k_malloc(size_t size);             /*  Allocate memory */
void k_free(void *ptr);                  /*  Free allocated memory */
void k_memory_init(void);                /*  Initialize the memory subsystem */
void k_heap_init(void);

/*  Global function declarations */
mem_block_t *k_find_free_block(size_t size); /*  Find a free memory block */
mem_block_t *k_expand_heap(size_t size);     /*  Expand the heap if needed */
void memset(void *ptr, char value, size_t size);
void map_page(uintptr_t virtual_address, uintptr_t physical_address, uint64_t flags);
#endif /*  MEMORY_H */
