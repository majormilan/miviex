#include <kernel/mm/pmm.h>
#include <kernel/hal/multiboot.h>
#include <stddef.h>
#include <stdint.h>
#include <kernel/video/vga.h>

#define PAGE_SIZE 4096
#define MAX_MEMORY_SIZE (1024 * 1024 * 1024) // 1GB
#define BITMAP_SIZE (MAX_MEMORY_SIZE / PAGE_SIZE / 8)

static uint8_t bitmap[BITMAP_SIZE];

extern uint8_t __bss_end[];
extern uint32_t initramfs_start;
extern uint32_t initramfs_end;

void pmm_init() {
    const memory_map_entry_t* memory_map = get_memory_map();
    size_t memory_map_size = get_memory_map_size();

    // Initially, mark all pages as used
    for (size_t i = 0; i < BITMAP_SIZE; i++) {
        bitmap[i] = 0xFF;
    }

    // Mark available pages as free
    for (size_t i = 0; i < memory_map_size; i++) {
        if (memory_map[i].type == MULTIBOOT_MEMORY_AVAILABLE) {
            uint64_t start = memory_map[i].address;
            uint64_t end = start + memory_map[i].length;

            for (uint64_t j = start; j < end; j += PAGE_SIZE) {
                if (j / PAGE_SIZE / 8 < BITMAP_SIZE) {
                    bitmap[(j / PAGE_SIZE) / 8] &= ~(1 << ((j / PAGE_SIZE) % 8));
                }
            }
        }
    }

    // Mark kernel and initramfs regions as used
    uint64_t kernel_start = 0x100000; // 1MB
    uint64_t kernel_end = (uint64_t)__bss_end;

    // Align kernel_end to the next page boundary
    if (kernel_end % PAGE_SIZE != 0) {
        kernel_end = (kernel_end / PAGE_SIZE + 1) * PAGE_SIZE;
    }

    for (uint64_t i = kernel_start; i < kernel_end; i += PAGE_SIZE) {
        if (i / PAGE_SIZE / 8 < BITMAP_SIZE) {
            bitmap[(i / PAGE_SIZE) / 8] |= (1 << ((i / PAGE_SIZE) % 8));
        }
    }

    // Mark initramfs region as used
    if (initramfs_start != 0 && initramfs_end != 0) {
        uint64_t initramfs_aligned_start = initramfs_start & ~(PAGE_SIZE - 1);
        uint64_t initramfs_aligned_end = (initramfs_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        for (uint64_t i = initramfs_aligned_start; i < initramfs_aligned_end; i += PAGE_SIZE) {
            if (i / PAGE_SIZE / 8 < BITMAP_SIZE) {
                bitmap[(i / PAGE_SIZE) / 8] |= (1 << ((i / PAGE_SIZE) % 8));
            }
        }
    }

    terminal_print_colorful("PMM: Initialized. Kernel end: ", VGA_COLOR_LIGHT_CYAN);
    terminal_print_hex(kernel_end);
    terminal_print_colorful(" Initramfs end: ", VGA_COLOR_LIGHT_CYAN);
    terminal_print_hex(initramfs_end);
    terminal_print_colorful("\n", VGA_COLOR_LIGHT_CYAN);
}

void* pmm_alloc_page() {
    for (size_t i = 0; i < BITMAP_SIZE; i++) {
        if (bitmap[i] != 0xFF) {
            for (size_t j = 0; j < 8; j++) {
                if (!(bitmap[i] & (1 << j))) {
                    bitmap[i] |= (1 << j);
                    return (void*)((i * 8 + j) * PAGE_SIZE);
                }
            }
        }
    }
    return NULL; // Out of memory
}

void pmm_free_page(void* page) {
    uint64_t addr = (uint64_t)page;
    bitmap[(addr / PAGE_SIZE) / 8] &= ~(1 << ((addr / PAGE_SIZE) % 8));
}
