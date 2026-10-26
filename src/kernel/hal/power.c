#include <kernel/hal/power.h>
#include <kernel/hal/io.h>
#include <kernel/hal/acpi.h>
#include <kernel/proc/scheduler.h>
#include <kernel/proc/signal.h>
#include <kernel/log.h>

/*
 * ACPI first (FADT reset register, \_S5 sleep state); then what PCs and the
 * common hypervisors have always offered: the keyboard controller's reset
 * line, triple faults, and their PM control ports at fixed addresses.
 */

static int cad_restart = 1;

static void stop(void) {
    for (;;) {
        asm volatile("cli; hlt");
    }
}

void power_restart(void) {
    klog(LOG_INFO, "power", "Restarting system");
    asm volatile("cli");
    acpi_reset();
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) {
        ; /* wait for the 8042 input buffer */
    }
    outb(0x64, 0xFE); /* pulse the reset line */

    /* Still here: a triple fault resets any x86. */
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) none = { 0, 0 };
    asm volatile("lidt %0; int3" : : "m"(none));
    stop();
    __builtin_unreachable();
}

void power_off(void) {
    klog(LOG_INFO, "power", "Power down");
    asm volatile("cli");
    acpi_poweroff();
    outw(0x604, 0x2000);  /* no ACPI S5: QEMU (PIIX4 / ICH9 PM) */
    outw(0xB004, 0x2000); /* Bochs, older QEMU */
    outw(0x4004, 0x3400); /* VirtualBox */
    for (int i = 0; i < 200000; i++) {
        inb(0x80); /* ~200 ms for the hypervisor to act on it */
    }
    klog(LOG_INFO, "power", "Poweroff failed; system halted");
    stop();
    __builtin_unreachable();
}

void power_halt(void) {
    klog(LOG_INFO, "power", "System halted");
    stop();
    __builtin_unreachable();
}

void power_set_cad(int restart) {
    cad_restart = restart;
}

void power_ctrl_alt_del(void) {
    if (cad_restart) {
        power_restart();
    }
    signal_send(process_find(1), SIGINT);
}
