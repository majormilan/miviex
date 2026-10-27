#include <kernel/drivers/keyboard.h>
#include <kernel/hal/idt.h>
#include <kernel/hal/gdt.h>
#include <kernel/hal/pit.h>
#include <kernel/hal/clock.h>
#include <kernel/hal/acpi.h>
#include <kernel/hal/apic.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/device.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <kernel/video/vga.h>
#include <kernel/hal/pic.h>
#include <kernel/vfs/vfs.h>
#include <kernel/libc/string.h>
#include <kernel/hal/multiboot.h>
#include <kernel/syscall/syscall.h>
#include <kernel/vfs/initramfs.h>
#include <kernel/proc/scheduler.h>
#include <kernel/proc/exec.h>
#include <kernel/drivers/tty.h>
#include <kernel/drivers/random.h>
#include <kernel/log.h>
#include <kernel/hal/io.h>

extern uint32_t initramfs_start; // Declare global initramfs_start
extern uint32_t initramfs_end;   // Declare global initramfs_end


// Timer tick rate driving the scheduler's preemption (see scheduler_tick()).
#define SCHEDULER_TICK_HZ 100

int enable_interrupts() {
    asm volatile("sti");
    return 0;
}

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

static int pit_init_wrapper() {
    pit_init(SCHEDULER_TICK_HZ);
    return irq_request(0, isr_timer_handler);
}

static int scheduler_init_wrapper() {
    scheduler_init();
    return 0;
}

// The idle process: MINIX-style lowest priority (PRIO_IDLE), always
// runnable so the scheduler never has nothing to pick. Whatever an
// interrupt woke runs as soon as the interrupt is over, not when the idle
// quantum ends; "sti; hlt" cannot miss an interrupt in between.
static void idle_process_entry(void *arg) {
    (void)arg;
    while (1) {
        __asm__ volatile("cli");
        scheduler_yield();
        __asm__ volatile("sti; hlt");
    }
}

void kernel_main(uint32_t mbi) {
    serial_init();
    terminal_clear();
    klog(LOG_INFO, "Kernel", "Booting MiViE UNIX...");

    parse_multiboot_info((uint64_t*)phys_to_virt(mbi));
    klog_init(get_multiboot_cmdline());
    klog(LOG_INFO, "Multiboot", "Parsed info");

    klog_execute(gdt_init, "GDT", "Initialized");
    klog_execute(k_memory_init, "MM", "Initialized");

    address_space_t* kernel_address_space = vmm_create_address_space();
    vmm_switch_address_space(kernel_address_space);
    klog(LOG_OK, "VMM", "Address space created and switched");
    acpi_init();

    klog_execute(init_idt, "IDT", "Initialized");
    klog_execute(init_syscalls, "Syscall", "Initialized");
    klog_execute(pic_remap_wrapper, "PIC", "Remapped");
    irq_init();
    klog_execute(pit_init_wrapper, "PIT", "Programmed");
    clock_init();
    random_init();
    klog_execute(enable_interrupts, "Interrupts", "Enabled");
    klog_execute(vfs_init_wrapper, "VFS", "Initialized");
    mem_init();
    klog_execute(keyboard_init, "Keyboard", "Initialized");
    tty_init(get_multiboot_cmdline());
    pci_init();
    klog(LOG_OK, "TTY", "Console is /dev/%s", tty_console->name);

    // Parse initramfs if found
    if (initramfs_start != 0 && initramfs_end != 0) {
        if (initramfs_parse(initramfs_start, initramfs_end) == 0) {
            klog(LOG_OK, "Initramfs", "Parsed");
        } else {
            klog(LOG_FAIL, "Initramfs", "Parse failed");
        }
    } else {
        klog(LOG_INFO, "Initramfs", "Not found");
    }

    klog_execute(scheduler_init_wrapper, "Scheduler", "Initialized");

    // Created first so they get Linux's numbering: idle is 0, init is 1.
    process_create(idle_process_entry, NULL, PRIO_IDLE);
    if (process_exec("/init", PRIO_USER) == NULL) {
        klog(LOG_FAIL, "Kernel", "No working /init found");
    }

    klog(LOG_INFO, "Kernel", "Boot sequence complete, handing off to scheduler.");

    scheduler_start(); // Does not return.

    while (1) {
        __asm__("hlt");
    }
}

