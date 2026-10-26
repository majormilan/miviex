#include <kernel/hal/apic.h>
#include <kernel/hal/acpi.h>
#include <kernel/hal/io.h>
#include <kernel/mm/vmm.h>
#include <kernel/log.h>

#define LAPIC_ID    0x20
#define LAPIC_TPR   0x80
#define LAPIC_EOI   0xB0
#define LAPIC_SVR   0xF0
#define LAPIC_TIMER 0x320
#define LAPIC_LINT0 0x350
#define LAPIC_LINT1 0x360
#define LAPIC_ERROR 0x370
#define LVT_MASKED  0x10000
#define LVT_NMI     0x400
#define SPURIOUS    0xFF
#define MSR_APIC_BASE 0x1B

#define FIRST_DYNAMIC 48
#define LAST_DYNAMIC  239

static volatile uint32_t *lapic;
static volatile uint32_t *ioapic[ACPI_MAX_IOAPICS];
static uint32_t ioapic_pins[ACPI_MAX_IOAPICS];
static bool apic;
static uint8_t next_vector = FIRST_DYNAMIC;

static uint32_t lapic_read(uint32_t reg) {
    return lapic[reg / 4];
}

static void lapic_write(uint32_t reg, uint32_t v) {
    lapic[reg / 4] = v;
}

static uint32_t io_read(volatile uint32_t *io, uint8_t reg) {
    io[0] = reg;
    return io[4];
}

static void io_write(volatile uint32_t *io, uint8_t reg, uint32_t v) {
    io[0] = reg;
    io[4] = v;
}

uint8_t lapic_id(void) {
    return apic ? (uint8_t)(lapic_read(LAPIC_ID) >> 24) : 0;
}

bool irq_apic(void) {
    return apic;
}

/* Program the I/O APIC pin of a GSI: vector, polarity, trigger, CPU. */
static int route_gsi(uint32_t gsi, uint8_t vector, bool low, bool level) {
    for (uint32_t i = 0; i < acpi.nioapic; i++) {
        uint32_t base = acpi.ioapic[i].gsi_base;
        if (gsi >= base && gsi < base + ioapic_pins[i]) {
            uint8_t reg = (uint8_t)(0x10 + 2 * (gsi - base));
            io_write(ioapic[i], (uint8_t)(reg + 1), (uint32_t)lapic_id() << 24);
            io_write(ioapic[i], reg, vector | (low ? 1u << 13 : 0) | (level ? 1u << 15 : 0));
            return 0;
        }
    }
    return -1;
}

static void lapic_init(void) {
    lapic = vmm_ioremap(acpi.lapic, 0x400, true);
    wrmsr(MSR_APIC_BASE, rdmsr(MSR_APIC_BASE) | 1u << 11);
    lapic_write(LAPIC_TPR, 0);
    lapic_write(LAPIC_TIMER, LVT_MASKED);
    lapic_write(LAPIC_LINT0, LVT_MASKED); /* the 8259 is not used */
    lapic_write(LAPIC_LINT1, LVT_NMI);
    lapic_write(LAPIC_ERROR, LVT_MASKED);
    lapic_write(LAPIC_SVR, 0x100 | SPURIOUS);
}

void irq_init(void) {
    if (!acpi.present || acpi.nioapic == 0 || acpi.lapic == 0) {
        klog(LOG_INFO, "irq", "No I/O APIC: using the 8259 PICs");
        return;
    }
    for (uint32_t i = 0; i < acpi.nioapic; i++) {
        ioapic[i] = vmm_ioremap(acpi.ioapic[i].addr, 0x20, true);
        ioapic_pins[i] = ((io_read(ioapic[i], 1) >> 16) & 0xFF) + 1;
        for (uint32_t p = 0; p < ioapic_pins[i]; p++) {
            io_write(ioapic[i], (uint8_t)(0x10 + 2 * p), 1u << 16); /* masked */
        }
    }
    lapic_init();
    outb(0x21, 0xFF); /* the 8259s stay remapped and fully masked */
    outb(0xA1, 0xFF);
    apic = true;
    klog(LOG_OK, "irq", "Local APIC %u, %u I/O APIC(s) with %u pins", lapic_id(),
         acpi.nioapic, ioapic_pins[0]);
}

int irq_request(int irq, isr_t handler) {
    uint8_t vector = (uint8_t)(32 + irq);

    if (irq < 0 || irq > 15) {
        return -1;
    }
    add_interrupt_handler(vector, handler);
    if (!apic) {
        uint16_t port = irq < 8 ? 0x21 : 0xA1;
        outb(port, inb(port) & ~(1u << (irq & 7)));
        if (irq >= 8) {
            outb(0x21, inb(0x21) & ~(1u << 2)); /* cascade */
        }
        return 0;
    }
    /* ISA defaults (edge, active high) unless the MADT overrides them. */
    uint16_t f = acpi.isa_flags[irq];
    return route_gsi(acpi.isa_gsi[irq], vector, (f & 3) == 3, ((f >> 2) & 3) == 3);
}

uint8_t irq_alloc_vector(isr_t handler) {
    if (next_vector > LAST_DYNAMIC) {
        return 0;
    }
    add_interrupt_handler(next_vector, handler);
    return next_vector++;
}

void irq_eoi(uint64_t vector) {
    if (vector == SPURIOUS) {
        return;
    }
    if (apic) {
        lapic_write(LAPIC_EOI, 0);
        return;
    }
    if (vector >= 40 && vector < 48) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}
