#include <kernel/drivers/keyboard.h>
#include <kernel/hal/idt.h>
#include <kernel/hal/gdt.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <kernel/video/vga.h>
#include <kernel/hal/pic.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/hal/multiboot.h>
#include <kernel/syscall/syscall.h>
#include <kernel/vfs/initramfs.h>
#include <kernel/log.h>

extern uint32_t initramfs_start; // Declare global initramfs_start
extern uint32_t initramfs_end;   // Declare global initramfs_end


#define O_CREAT 0x01
#define O_RDWR  0x02

int enable_interrupts() {
    asm volatile("sti");
    return 0;
}

void trigger_interrupt_0() { asm volatile("int $0"); }

extern unsigned char* __bss_start;
extern unsigned char* __bss_end;

static int vfs_init_wrapper() {
    dentry_t* fs_root = vfs_init();
    if (fs_root != NULL) {
        return 0; // Success
    } else {
        return -1; // Failure
    }
}

extern void kernel_main(void) {
    terminal_clear();
    klog(LOG_INFO, "Kernel", "Booting MiViE UNIX...");

    uint32_t ebx_at_start = *(uint32_t*)0x8004;
    parse_multiboot_info((uint64_t*)(uint64_t)ebx_at_start);
    klog(LOG_INFO, "Multiboot", "Parsed info");

    klog_execute(gdt_init, "GDT", "Initialized");
    klog_execute(k_memory_init, "MM", "Initialized");

    address_space_t* kernel_address_space = vmm_create_address_space();
    vmm_switch_address_space(kernel_address_space);
    klog(LOG_OK, "VMM", "Address space created and switched");

    klog_execute(init_idt, "IDT", "Initialized");
    klog_execute(init_syscalls, "Syscall", "Initialized");
    klog_execute(pic_remap_wrapper, "PIC", "Remapped");
    klog_execute(enable_interrupts, "Interrupts", "Enabled");
    klog_execute(keyboard_init, "Keyboard", "Initialized");
    klog_execute(vfs_init_wrapper, "VFS", "Initialized");

    // Parse initramfs if found
    if (initramfs_start != 0 && initramfs_end != 0) {
        initramfs_parse(initramfs_start, initramfs_end);
        klog(LOG_OK, "Initramfs", "Parsed");
    } else {
        klog(LOG_INFO, "Initramfs", "Not found");
    }

    klog(LOG_INFO, "Kernel", "Boot sequence complete, entering idle loop.");

    while (1) {
        char c;
        if (keyboard_get_char(&c)) {
            if (c == '\b') {
                terminal_backspace();
            } else {
                terminal_putchar(c);
            }
        }

        __asm__("hlt");
    }
}

