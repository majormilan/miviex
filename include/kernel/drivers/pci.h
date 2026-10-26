#ifndef PCI_H
#define PCI_H

#include <kernel/types.h>
#include <stdint.h>

typedef struct {
    uint64_t addr;  /* physical address or I/O port; 0 if unused */
    uint64_t size;
    bool io, prefetch;
} pci_bar_t;

typedef struct {
    uint8_t bus, dev, fn;
    uint16_t vendor, device;
    uint8_t class, subclass, progif, revision;
    uint8_t irq_line, irq_pin;  /* legacy INTx: ISA line, pin A-D (1-4) */
    uint8_t msi_cap, msix_cap;  /* capability offsets, 0 if absent */
    pci_bar_t bar[6];
} pci_dev_t;

void pci_init(void);
uint32_t pci_count(void);
pci_dev_t *pci_get(uint32_t i);
/* Next device after `from` (NULL: the first) of class/subclass; 0xFF
 * matches any subclass. */
pci_dev_t *pci_find_class(pci_dev_t *from, uint8_t class, uint8_t subclass);
pci_dev_t *pci_find_id(pci_dev_t *from, uint16_t vendor, uint16_t device);

uint32_t pci_read32(pci_dev_t *d, uint8_t off);
uint16_t pci_read16(pci_dev_t *d, uint8_t off);
void pci_write32(pci_dev_t *d, uint8_t off, uint32_t v);
void pci_write16(pci_dev_t *d, uint8_t off, uint16_t v);

/* Turn on decoding of its regions and, for DMA, bus mastering. */
void pci_enable(pci_dev_t *d, bool busmaster);
/* Route its interrupts to vector through MSI (0, or -1 without MSI). */
int pci_enable_msi(pci_dev_t *d, uint8_t vector);

#endif /* PCI_H */
