#ifndef ACPI_H
#define ACPI_H

#include <kernel/types.h>
#include <stdint.h>

#define ACPI_MAX_IOAPICS 8

/* What the rest of the kernel needs from the ACPI tables. */
typedef struct {
    bool present;
    uint64_t lapic;               /* local APIC registers */
    uint32_t nioapic;
    struct {
        uint8_t id;
        uint64_t addr;
        uint32_t gsi_base;
    } ioapic[ACPI_MAX_IOAPICS];
    uint32_t isa_gsi[16];         /* ISA IRQ -> GSI after overrides */
    uint16_t isa_flags[16];       /* MPS INTI polarity/trigger flags */
    uint32_t ncpus;
    uint8_t century;              /* CMOS register, 0 if none */
    uint64_t hpet, mcfg;          /* table addresses, 0 if absent */
} acpi_info_t;

extern acpi_info_t acpi;

void acpi_init(void);
/* Find a table by signature (mapped), NULL if absent. */
const void *acpi_table(const char sig[4]);
/* Return only if the firmware would not do it. */
void acpi_poweroff(void);
void acpi_reset(void);

#endif /* ACPI_H */
