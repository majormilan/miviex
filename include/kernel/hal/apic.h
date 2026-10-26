#ifndef APIC_H
#define APIC_H

#include <kernel/types.h>
#include <kernel/hal/isr.h>
#include <stdint.h>

/*
 * Interrupt routing: the local APIC and I/O APIC(s) from the ACPI MADT,
 * or the 8259 PICs on machines without them. ISA IRQ n is vector 32 + n
 * either way; MSI vectors come from irq_alloc_vector().
 */
void irq_init(void);
/* Route ISA IRQ `irq` (0-15) to its vector and add a handler; lines can
 * be shared. 0 or -1. */
int irq_request(int irq, isr_t handler);
/* A free vector for MSI with this handler, or 0. */
uint8_t irq_alloc_vector(isr_t handler);
void irq_eoi(uint64_t vector);
bool irq_apic(void);       /* routed through the APIC? */
uint8_t lapic_id(void);

#endif /* APIC_H */
