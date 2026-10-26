#include <kernel/mm/pmm.h>
#include <kernel/hal/multiboot.h>
#include <kernel/log.h>
#include <kernel/libc/string.h>
#include <stddef.h>
#include <stdint.h>

/* Linker symbols: physical load addresses under the current identity map. */
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static uint8_t *bitmap = NULL;
static uint64_t total_frames = 0;
static uint64_t used_frames = 0;
static uint64_t detected_ram = 0;
static uint64_t managed_bytes = 0;
static phys_addr_t bitmap_phys = 0;
static size_t bitmap_bytes = 0;

static inline uint64_t frame_index(phys_addr_t addr) {
    return addr / PAGE_SIZE;
}

static inline bool frame_in_range(phys_addr_t addr) {
    return addr < managed_bytes && (addr % PAGE_SIZE) == 0;
}

static bool bitmap_test(uint64_t frame) {
    return (bitmap[frame / 8] & (1u << (frame % 8))) != 0;
}

static void bitmap_set(uint64_t frame) {
    bitmap[frame / 8] |= (uint8_t)(1u << (frame % 8));
}

static void bitmap_clear(uint64_t frame) {
    bitmap[frame / 8] &= (uint8_t)~(1u << (frame % 8));
}

/* Mark [start, end) as used. Addresses need not be page-aligned. */
static void pmm_mark_used_range(phys_addr_t start, phys_addr_t end) {
    if (end <= start) {
        return;
    }

    phys_addr_t lo = align_down_page(start);
    phys_addr_t hi = align_up_page(end);

    if (lo >= managed_bytes) {
        return;
    }
    if (hi > managed_bytes) {
        hi = managed_bytes;
    }

    for (phys_addr_t addr = lo; addr < hi; addr += PAGE_SIZE) {
        uint64_t frame = frame_index(addr);
        if (!bitmap_test(frame)) {
            bitmap_set(frame);
            used_frames++;
        }
    }
}

/* Mark [start, end) as free: only frames currently marked used. */
static void pmm_mark_free_range(phys_addr_t start, phys_addr_t end) {
    if (end <= start) {
        return;
    }

    /* Free only whole pages fully contained in the range. */
    phys_addr_t lo = align_up_page(start);
    phys_addr_t hi = align_down_page(end);

    if (lo >= managed_bytes) {
        return;
    }
    if (hi > managed_bytes) {
        hi = managed_bytes;
    }

    for (phys_addr_t addr = lo; addr < hi; addr += PAGE_SIZE) {
        uint64_t frame = frame_index(addr);
        if (bitmap_test(frame)) {
            bitmap_clear(frame);
            if (used_frames > 0) {
                used_frames--;
            }
        }
    }
}

static uint64_t compute_detected_ram(void) {
    const memory_map_entry_t *map = get_memory_map();
    size_t n = get_memory_map_size();
    uint64_t total = 0;

    for (size_t i = 0; i < n; i++) {
        if (map[i].type == MULTIBOOT_MEMORY_AVAILABLE) {
            total += map[i].length;
        }
    }
    return total;
}

/*
 * Highest physical address we can manage: limited by the boot identity map
 * and by whatever AVAILABLE memory Multiboot reported below that ceiling.
 */
static uint64_t compute_managed_limit(void) {
    const memory_map_entry_t *map = get_memory_map();
    size_t n = get_memory_map_size();
    uint64_t limit = 0;

    for (size_t i = 0; i < n; i++) {
        if (map[i].type != MULTIBOOT_MEMORY_AVAILABLE) {
            continue;
        }

        uint64_t start = map[i].address;
        uint64_t end = start + map[i].length;

        if (start >= PMM_IDENTITY_MAP_END) {
            continue;
        }
        if (end > PMM_IDENTITY_MAP_END) {
            end = PMM_IDENTITY_MAP_END;
        }
        if (end > limit) {
            limit = end;
        }
    }

    /* Page-align down so the bitmap covers whole frames only. */
    return align_down_page(limit);
}

/*
 * Ranges the bootloader handed us that must survive pmm_init: page 0 and
 * legacy low memory, the kernel image, the Multiboot info, and modules.
 */
typedef struct {
    phys_addr_t start;
    phys_addr_t end;
} phys_range_t;

#define PMM_MAX_RESERVED (3 + MULTIBOOT_MAX_MODULES)

static phys_range_t reserved[PMM_MAX_RESERVED];
static size_t reserved_count = 0;

static void add_reserved(phys_addr_t start, phys_addr_t end) {
    if (end <= start || reserved_count == PMM_MAX_RESERVED) {
        return;
    }
    reserved[reserved_count].start = align_down_page(start);
    reserved[reserved_count].end = align_up_page(end);
    reserved_count++;
}

static void collect_reserved(void) {
    reserved_count = 0;

    /* Low 1 MiB: page 0 (null guard), IVT, BDA, EBDA, VGA, option ROMs. */
    add_reserved(0, 0x100000);
    add_reserved((phys_addr_t)(uintptr_t)_kernel_start,
                 (phys_addr_t)(uintptr_t)_kernel_end);

    uint64_t mb_addr = get_multiboot_info_addr();
    add_reserved(mb_addr, mb_addr + get_multiboot_info_size());

    const multiboot_module_t *mods = get_multiboot_modules();
    size_t mod_count = get_multiboot_module_count();
    for (size_t i = 0; i < mod_count; i++) {
        add_reserved(mods[i].start, mods[i].end);
    }
}

/*
 * Find `len` bytes of AVAILABLE memory inside the managed window that do
 * not overlap any reserved range. GRUB loads modules right after the
 * kernel, so "just past _kernel_end" is not safe.
 */
static phys_addr_t find_bitmap_home(size_t len) {
    const memory_map_entry_t *map = get_memory_map();
    size_t n = get_memory_map_size();

    for (size_t i = 0; i < n; i++) {
        if (map[i].type != MULTIBOOT_MEMORY_AVAILABLE) {
            continue;
        }

        phys_addr_t start = align_up_page(map[i].address);
        phys_addr_t end = align_down_page(map[i].address + map[i].length);
        if (end > managed_bytes) {
            end = managed_bytes;
        }

        size_t r = 0;
        while (r < reserved_count && start + len <= end) {
            if (start < reserved[r].end && reserved[r].start < start + len) {
                start = reserved[r].end;
                r = 0;
            } else {
                r++;
            }
        }
        if (start + len <= end) {
            return start;
        }
    }
    return PHYS_ADDR_INVALID;
}

void pmm_init(void) {
    detected_ram = compute_detected_ram();
    managed_bytes = compute_managed_limit();

    if (managed_bytes < PAGE_SIZE * 16) {
        klog(LOG_FAIL, "pmm", "No usable identity-mapped RAM from Multiboot mmap");
        bitmap = NULL;
        total_frames = 0;
        used_frames = 0;
        return;
    }

    total_frames = managed_bytes / PAGE_SIZE;
    bitmap_bytes = (total_frames + 7) / 8;
    size_t bitmap_pages = (bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    collect_reserved();
    bitmap_phys = find_bitmap_home(bitmap_pages * PAGE_SIZE);
    if (bitmap_phys == PHYS_ADDR_INVALID) {
        klog(LOG_FAIL, "pmm", "No free region large enough for frame bitmap");
        bitmap = NULL;
        total_frames = 0;
        return;
    }

    bitmap = (uint8_t *)phys_to_virt(bitmap_phys);
    memset(bitmap, 0xFF, bitmap_pages * PAGE_SIZE); /* all used until freed */
    used_frames = total_frames;

    /* Free Multiboot AVAILABLE regions that fall inside the managed window. */
    const memory_map_entry_t *map = get_memory_map();
    size_t map_size = get_memory_map_size();
    for (size_t i = 0; i < map_size; i++) {
        if (map[i].type != MULTIBOOT_MEMORY_AVAILABLE) {
            continue;
        }
        phys_addr_t start = map[i].address;
        phys_addr_t end = start + map[i].length;
        pmm_mark_free_range(start, end);
    }

    /* Re-reserve what the kernel and bootloader still need, then the bitmap. */
    for (size_t i = 0; i < reserved_count; i++) {
        pmm_mark_used_range(reserved[i].start, reserved[i].end);
    }
    pmm_mark_used_range(bitmap_phys, bitmap_phys + bitmap_pages * PAGE_SIZE);

    pmm_stats_t st;
    pmm_get_stats(&st);

    klog(LOG_OK, "pmm",
         "Ready: detected %d MiB RAM, managing %d MiB (identity map), "
         "%d frames free / %d total, bitmap at 0x%x (%d pages)",
         (int)(st.detected_ram / (1024 * 1024)),
         (int)(st.managed_bytes / (1024 * 1024)),
         (int)st.free_frames,
         (int)st.total_frames,
         (unsigned)bitmap_phys,
         (int)bitmap_pages);

    if (st.detected_ram > st.managed_bytes) {
        klog(LOG_INFO, "pmm",
             "%d MiB above identity map is visible but not allocatable yet "
             "(needs HHDM / extended page tables)",
             (int)((st.detected_ram - st.managed_bytes) / (1024 * 1024)));
    }
}

phys_addr_t pmm_alloc_page(void) {
    return pmm_alloc_pages(1);
}

phys_addr_t pmm_alloc_pages(size_t count) {
    if (count == 0 || bitmap == NULL || total_frames == 0) {
        return PHYS_ADDR_INVALID;
    }
    if (count > total_frames) {
        return PHYS_ADDR_INVALID;
    }

    /* First-fit scan for `count` consecutive free frames. */
    uint64_t run = 0;
    uint64_t run_start = 0;

    for (uint64_t frame = 0; frame < total_frames; frame++) {
        if (!bitmap_test(frame)) {
            if (run == 0) {
                run_start = frame;
            }
            run++;
            if (run == count) {
                for (uint64_t f = run_start; f < run_start + count; f++) {
                    bitmap_set(f);
                }
                used_frames += count;
                return run_start * PAGE_SIZE;
            }
        } else {
            run = 0;
        }
    }

    return PHYS_ADDR_INVALID;
}

void pmm_free_page(phys_addr_t page) {
    pmm_free_pages(page, 1);
}

void pmm_free_pages(phys_addr_t page, size_t count) {
    if (count == 0 || bitmap == NULL) {
        return;
    }
    if (!frame_in_range(page)) {
        klog(LOG_FAIL, "pmm", "pmm_free_pages: invalid address 0x%lx", (uint64_t)page);
        return;
    }
    if (count > total_frames || frame_index(page) + count > total_frames) {
        klog(LOG_FAIL, "pmm", "pmm_free_pages: range out of bounds (phys=0x%lx count=%d)",
             (uint64_t)page, (int)count);
        return;
    }

    uint64_t start = frame_index(page);
    for (uint64_t f = start; f < start + count; f++) {
        if (!bitmap_test(f)) {
            klog(LOG_FAIL, "pmm", "pmm_free_pages: double-free at 0x%lx",
                 (uint64_t)(f * PAGE_SIZE));
            return;
        }
    }
    for (uint64_t f = start; f < start + count; f++) {
        bitmap_clear(f);
    }
    used_frames -= count;
}

void pmm_get_stats(pmm_stats_t *out) {
    if (!out) {
        return;
    }
    out->total_frames = total_frames;
    out->used_frames = used_frames;
    out->free_frames = (used_frames <= total_frames) ? (total_frames - used_frames) : 0;
    out->usable_bytes = out->free_frames * PAGE_SIZE;
    out->detected_ram = detected_ram;
    out->managed_bytes = managed_bytes;
}
