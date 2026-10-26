#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <kernel/types.h>
#include <stdint.h>

#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_CMDLINE 1
#define MULTIBOOT_TAG_TYPE_MODULE 3
#define MULTIBOOT_TAG_TYPE_MMAP 6

#define MULTIBOOT_MEMORY_AVAILABLE 1
#define MULTIBOOT_MEMORY_RESERVED 2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS 4
#define MULTIBOOT_MEMORY_BADRAM 5

#define MULTIBOOT_MAX_MODULES 8
#define MULTIBOOT_MAX_MMAP_ENTRIES 128

typedef struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_tag_module_t;

typedef struct multiboot_tag {
    uint32_t type;
    uint32_t size;
} multiboot_tag_t;

typedef struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
} __attribute__((packed)) multiboot_tag_mmap_t;

typedef struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
} __attribute__((packed)) multiboot_mmap_entry_t;

typedef struct {
    uint64_t address;
    uint64_t length;
    uint32_t type;
} memory_map_entry_t;

typedef struct {
    uint64_t start; /* Inclusive physical start */
    uint64_t end;   /* Exclusive physical end */
} multiboot_module_t;

void parse_multiboot_info(uint64_t *multiboot_ptr);

const memory_map_entry_t *get_memory_map(void);
size_t get_memory_map_size(void);

/* Physical range of the Multiboot information structure itself. */
uint64_t get_multiboot_info_addr(void);
uint32_t get_multiboot_info_size(void);

/* Kernel command line from the boot loader ("" if none). */
const char *get_multiboot_cmdline(void);

const multiboot_module_t *get_multiboot_modules(void);
size_t get_multiboot_module_count(void);

/* Legacy single-module accessors (first Multiboot module = initramfs). */
extern uint32_t initramfs_start;
extern uint32_t initramfs_end;

#endif /* MULTIBOOT_H */
