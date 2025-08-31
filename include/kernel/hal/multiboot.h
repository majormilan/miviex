#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include <kernel/types.h>
#include <stdint.h>

#define MULTIBOOT_TAG_TYPE_END 0
#define MULTIBOOT_TAG_TYPE_MMAP 6
#define MULTIBOOT_TAG_TYPE_MODULE 3

typedef struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t string;
    uint32_t reserved;
} __attribute__((packed)) multiboot_tag_module_t;

#define MULTIBOOT_MEMORY_AVAILABLE 1

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

void parse_multiboot_info(uint64_t* multiboot_ptr);

typedef struct {
    uint64_t address;
    uint64_t length;
    uint32_t type;
} memory_map_entry_t;

const memory_map_entry_t* get_memory_map();
size_t get_memory_map_size();

#endif // MULTIBOOT_H