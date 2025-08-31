#include <kernel/hal/multiboot.h>
#include <kernel/video/vga.h>
#include <stdint.h>
#include <stddef.h>

#define MAX_MEMORY_MAP_ENTRIES 128

static memory_map_entry_t memory_map[MAX_MEMORY_MAP_ENTRIES];
static size_t memory_map_size = 0;

uint32_t initramfs_start = 0;
uint32_t initramfs_end = 0;

void parse_multiboot_info(uint64_t* multiboot_ptr) {
    uint64_t total_available_memory = 0;
    uint64_t total_reserved_ram = 0;
    uint64_t total_reserved_other = 0;

    uint32_t total_size = ((uint32_t*)multiboot_ptr)[0];

    for (multiboot_tag_t* tag = (multiboot_tag_t*)((uint8_t*)multiboot_ptr + 8);
         (uint8_t*)tag < (uint8_t*)multiboot_ptr + total_size && tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (multiboot_tag_t*)((uint8_t*)tag + ((tag->size + 7) & ~7))) {

        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            multiboot_tag_mmap_t* mmap_tag = (multiboot_tag_mmap_t*)tag;
            multiboot_mmap_entry_t* mmap_entry = (multiboot_mmap_entry_t*)((uint8_t*)tag + sizeof(multiboot_tag_mmap_t));
            multiboot_mmap_entry_t* mmap_end = (multiboot_mmap_entry_t*)((uint8_t*)tag + tag->size);

            while (mmap_entry < mmap_end && memory_map_size < MAX_MEMORY_MAP_ENTRIES) {
                memory_map[memory_map_size].address = mmap_entry->addr;
                memory_map[memory_map_size].length = mmap_entry->len;
                memory_map[memory_map_size].type = mmap_entry->type;
                memory_map_size++;

                if (mmap_entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    total_available_memory += mmap_entry->len;
                } else {
                    if (mmap_entry->addr < 0x100000000ULL) { // Below 4GB, likely reserved RAM
                        total_reserved_ram += mmap_entry->len;
                    } else { // Above 4GB, likely MMIO or other high reserved areas
                        total_reserved_other += mmap_entry->len;
                    }
                }
                mmap_entry = (multiboot_mmap_entry_t*)((uint8_t*)mmap_entry + mmap_tag->entry_size);
            }
        } else if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
            multiboot_tag_module_t* module_tag = (multiboot_tag_module_t*)tag;
            initramfs_start = module_tag->mod_start;
            initramfs_end = module_tag->mod_end;
            terminal_print_colorful("Initramfs found at: 0x", VGA_COLOR_GREEN);
            terminal_print_hex(initramfs_start);
            terminal_print_colorful(" - 0x", VGA_COLOR_GREEN);
            terminal_print_hex(initramfs_end);
            terminal_print_colorful(" (size: ", VGA_COLOR_GREEN);
            terminal_print_num(initramfs_end - initramfs_start);
            terminal_print_colorful(" bytes)\n", VGA_COLOR_GREEN);
        }
    }

    terminal_print_colorful("Available Memory: ", VGA_COLOR_GREEN);
    terminal_print_num(total_available_memory / (1024ULL * 1024ULL));
    terminal_print_colorful(" MB\n", VGA_COLOR_GREEN);
}

const memory_map_entry_t* get_memory_map() {
    return memory_map;
}

size_t get_memory_map_size() {
    return memory_map_size;
}
