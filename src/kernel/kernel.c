#include <kernel/drivers/keyboard.h>
#include <kernel/hal/idt.h>
#include <kernel/hal/gdt.h>
#include <kernel/hal/pit.h>
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
#include <kernel/log.h>

extern uint32_t initramfs_start; // Declare global initramfs_start
extern uint32_t initramfs_end;   // Declare global initramfs_end


#define O_CREAT 0x01
#define O_RDWR  0x02

// Timer tick rate driving the scheduler's preemption (see scheduler_tick()).
#define SCHEDULER_TICK_HZ 100

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

static int pit_init_wrapper() {
    pit_init(SCHEDULER_TICK_HZ);
    return 0;
}

static int scheduler_init_wrapper() {
    scheduler_init();
    return 0;
}

// The idle process: MINIX-style lowest priority (PRIO_IDLE), always
// runnable so the scheduler never has nothing to pick, and hosts the
// keyboard-echo loop that used to live directly in kernel_main().
static void idle_process_entry(void *arg) {
    (void)arg;
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

// Small demo processes proving the multi-level priority round-robin
// scheduler actually preempts and interleaves work across priority bands.
// Not meant to be permanent kernel fixtures.
static void demo_task_entry(void *arg) {
    const char *name = (const char*)arg;
    for (int i = 0; i < 5; i++) {
        klog(LOG_DEBUG, "demo", "%s: iteration %d (pid=%d)", name, i, scheduler_current()->pid);
        for (volatile int j = 0; j < 3000000; j++); // busy-wait so timer preemption is observable
    }
}

// Minimal ring-3 (CPL=3) demo payload, hand-assembled (see nasm source
// below) rather than linked as a separate flat binary, to avoid adding a
// second link step to the build. Equivalent to:
//   mov eax, 0                 ; syscall number 0 = puts
//   lea rdi, [rip + msg]       ; rdi = pointer to the string below
//   int 0x80                   ; invoke syscall from ring3 (needs DPL=3 gate)
//   jmp $                      ; loop forever (hlt is privileged, would GP fault here)
// msg: db "Hello from ring3!", 0
static const uint8_t ring3_demo_code[] = {
    0xb8, 0x00, 0x00, 0x00, 0x00,             // mov eax, 0
    0x48, 0x8d, 0x3d, 0x04, 0x00, 0x00, 0x00, // lea rdi, [rip+4]
    0xcd, 0x80,                               // int 0x80
    0xeb, 0xfe,                               // jmp $
    'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' ',
    'r', 'i', 'n', 'g', '3', '!', '\0'
};

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
    klog_execute(pit_init_wrapper, "PIT", "Programmed");
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

    klog_execute(scheduler_init_wrapper, "Scheduler", "Initialized");

    process_create(idle_process_entry, NULL, PRIO_IDLE);
    process_create(demo_task_entry, (void*)"system-task", PRIO_SYSTEM);
    process_create(demo_task_entry, (void*)"user-task", PRIO_USER);
    process_create_user(ring3_demo_code, sizeof(ring3_demo_code), PRIO_USER);

    klog(LOG_INFO, "Kernel", "Boot sequence complete, handing off to scheduler.");

    scheduler_start(); // Does not return.

    while (1) {
        __asm__("hlt");
    }
}

