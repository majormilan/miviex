#include <kernel/hal/multiboot.h>
#include <kernel/video/vga.h>
#include <stdint.h>

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

            while (mmap_entry < mmap_end) {
                uint32_t low_addr = ((uint32_t*)mmap_entry)[0];
                uint32_t high_addr = ((uint32_t*)mmap_entry)[1];
                uint64_t current_addr = ((uint64_t)high_addr << 32) | low_addr;

                uint32_t low_len = ((uint32_t*)mmap_entry)[2];
                uint32_t high_len = ((uint32_t*)mmap_entry)[3];
                uint64_t current_len = ((uint64_t)high_len << 32) | low_len;

                if (mmap_entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    total_available_memory += current_len;
                } else {
                    if (current_addr < 0x100000000ULL) { // Below 4GB, likely reserved RAM
                        total_reserved_ram += current_len;
                    } else { // Above 4GB, likely MMIO or other high reserved areas
                        total_reserved_other += current_len;
                    }
                }
                mmap_entry = (multiboot_mmap_entry_t*)((uint8_t*)mmap_entry + mmap_tag->entry_size);
            }
        }
    }

    terminal_print_colorful("Available Memory: ", VGA_COLOR_GREEN);
    terminal_print_num(total_available_memory / (1024ULL * 1024ULL));
    terminal_print_colorful(" MB\n", VGA_COLOR_GREEN);

}
