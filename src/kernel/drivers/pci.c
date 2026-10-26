#include <kernel/drivers/pci.h>
#include <kernel/hal/io.h>
#include <kernel/hal/apic.h>
#include <kernel/log.h>

/*
 * PCI through configuration mechanism #1 (ports 0xCF8/0xCFC), which every
 * PC and hypervisor provides; ECAM (MCFG) would add extended space only.
 */

#define MAX_DEVS 64

#define PCI_COMMAND  0x04
#define PCI_STATUS   0x06
#define PCI_HEADER   0x0E
#define PCI_BAR0     0x10
#define PCI_CAPS     0x34
#define PCI_IRQ_LINE 0x3C
#define CMD_IO       0x1
#define CMD_MEM      0x2
#define CMD_MASTER   0x4
#define CMD_INTX_OFF 0x400
#define STATUS_CAPS  0x10
#define CAP_MSI      0x05
#define CAP_MSIX     0x11

static pci_dev_t devs[MAX_DEVS];
static uint32_t ndevs;

static uint32_t addr(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return 0x80000000u | (uint32_t)bus << 16 | (uint32_t)dev << 11 | (uint32_t)fn << 8 | (off & 0xFC);
}

static uint32_t cfg_read(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    outl(0xCF8, addr(bus, dev, fn, off));
    return inl(0xCFC);
}

static void cfg_write(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t v) {
    outl(0xCF8, addr(bus, dev, fn, off));
    outl(0xCFC, v);
}

uint32_t pci_read32(pci_dev_t *d, uint8_t off) {
    return cfg_read(d->bus, d->dev, d->fn, off);
}

uint16_t pci_read16(pci_dev_t *d, uint8_t off) {
    return (uint16_t)(cfg_read(d->bus, d->dev, d->fn, off) >> ((off & 2) * 8));
}

void pci_write32(pci_dev_t *d, uint8_t off, uint32_t v) {
    cfg_write(d->bus, d->dev, d->fn, off, v);
}

void pci_write16(pci_dev_t *d, uint8_t off, uint16_t v) {
    uint32_t w = cfg_read(d->bus, d->dev, d->fn, off);
    int shift = (off & 2) * 8;

    w = (w & ~(0xFFFFu << shift)) | (uint32_t)v << shift;
    cfg_write(d->bus, d->dev, d->fn, off, w);
}

/* Size the regions by writing all ones, with decoding off meanwhile. */
static void read_bars(pci_dev_t *d) {
    uint16_t cmd = pci_read16(d, PCI_COMMAND);

    pci_write16(d, PCI_COMMAND, cmd & ~(CMD_IO | CMD_MEM));
    for (int i = 0; i < 6; i++) {
        uint8_t off = (uint8_t)(PCI_BAR0 + i * 4);
        uint32_t lo = pci_read32(d, off), mask;
        pci_bar_t *b = &d->bar[i];

        pci_write32(d, off, 0xFFFFFFFF);
        mask = pci_read32(d, off);
        pci_write32(d, off, lo);
        if (mask == 0) {
            continue;
        }
        if (lo & 1) {
            b->io = true;
            b->addr = lo & ~3u;
            b->size = (uint16_t)(~(mask & ~3u) + 1);
            continue;
        }
        b->prefetch = (lo & 8) != 0;
        b->addr = lo & ~0xFu;
        b->size = ~(uint64_t)(mask & ~0xFu) + 1;
        b->size &= 0xFFFFFFFF;
        if ((lo & 6) == 4 && i < 5) { /* 64-bit: the next BAR is the high half */
            uint8_t hoff = (uint8_t)(off + 4);
            uint32_t hi = pci_read32(d, hoff), hmask;
            pci_write32(d, hoff, 0xFFFFFFFF);
            hmask = pci_read32(d, hoff);
            pci_write32(d, hoff, hi);
            b->addr |= (uint64_t)hi << 32;
            b->size = ~((uint64_t)hmask << 32 | (mask & ~0xFu)) + 1;
            i++;
        }
    }
    pci_write16(d, PCI_COMMAND, cmd);
}

static void read_caps(pci_dev_t *d) {
    if (!(pci_read16(d, PCI_STATUS) & STATUS_CAPS)) {
        return;
    }
    uint8_t off = (uint8_t)(pci_read32(d, PCI_CAPS) & 0xFC);
    for (int guard = 0; off != 0 && guard < 48; guard++) {
        uint32_t c = pci_read32(d, off);
        if ((c & 0xFF) == CAP_MSI) {
            d->msi_cap = off;
        } else if ((c & 0xFF) == CAP_MSIX) {
            d->msix_cap = off;
        }
        off = (uint8_t)((c >> 8) & 0xFC);
    }
}

static const char *class_name(pci_dev_t *d) {
    static const char *const names[] = {
        "unclassified", "storage", "network", "display", "multimedia", "memory",
        "bridge", "communication", "system", "input", "docking", "processor", "serial bus",
    };
    if (d->class == 1) {
        static const char *const st[] = { "SCSI", "IDE", "floppy", "IPI", "RAID", "ATA",
                                           "SATA", "SAS", "NVMe" };
        if (d->subclass < 9) {
            return st[d->subclass];
        }
    }
    return d->class < 13 ? names[d->class] : "other";
}

static void probe(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t id = cfg_read(bus, dev, fn, 0), cls;
    pci_dev_t *d;

    if ((id & 0xFFFF) == 0xFFFF || ndevs == MAX_DEVS) {
        return;
    }
    d = &devs[ndevs++];
    d->bus = bus;
    d->dev = dev;
    d->fn = fn;
    d->vendor = (uint16_t)id;
    d->device = (uint16_t)(id >> 16);
    cls = cfg_read(bus, dev, fn, 8);
    d->class = (uint8_t)(cls >> 24);
    d->subclass = (uint8_t)(cls >> 16);
    d->progif = (uint8_t)(cls >> 8);
    d->revision = (uint8_t)cls;
    if ((cfg_read(bus, dev, fn, PCI_HEADER & 0xFC) >> 16 & 0x7F) == 0) { /* normal device */
        uint32_t irq = cfg_read(bus, dev, fn, PCI_IRQ_LINE);
        d->irq_line = (uint8_t)irq;
        d->irq_pin = (uint8_t)(irq >> 8);
        read_bars(d);
    }
    read_caps(d);
    klog(LOG_INFO, "pci", "%02x:%02x.%u %04x:%04x %s (%02x.%02x.%02x)%s%s%s",
         bus, dev, fn, d->vendor, d->device, class_name(d), d->class, d->subclass, d->progif,
         d->irq_pin ? " INTx" : "", d->msi_cap ? " MSI" : "", d->msix_cap ? " MSI-X" : "");
}

void pci_init(void) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            if ((cfg_read((uint8_t)bus, dev, 0, 0) & 0xFFFF) == 0xFFFF) {
                continue;
            }
            bool multi = cfg_read((uint8_t)bus, dev, 0, PCI_HEADER & 0xFC) >> 16 & 0x80;
            for (uint8_t fn = 0; fn < (multi ? 8 : 1); fn++) {
                probe((uint8_t)bus, dev, fn);
            }
        }
    }
    klog(LOG_OK, "pci", "%u device(s)", ndevs);
}

uint32_t pci_count(void) {
    return ndevs;
}

pci_dev_t *pci_get(uint32_t i) {
    return i < ndevs ? &devs[i] : NULL;
}

pci_dev_t *pci_find_class(pci_dev_t *from, uint8_t class, uint8_t subclass) {
    for (pci_dev_t *d = from != NULL ? from + 1 : devs; d < devs + ndevs; d++) {
        if (d->class == class && (subclass == 0xFF || d->subclass == subclass)) {
            return d;
        }
    }
    return NULL;
}

pci_dev_t *pci_find_id(pci_dev_t *from, uint16_t vendor, uint16_t device) {
    for (pci_dev_t *d = from != NULL ? from + 1 : devs; d < devs + ndevs; d++) {
        if (d->vendor == vendor && d->device == device) {
            return d;
        }
    }
    return NULL;
}

void pci_enable(pci_dev_t *d, bool busmaster) {
    uint16_t cmd = pci_read16(d, PCI_COMMAND) | CMD_IO | CMD_MEM;

    if (busmaster) {
        cmd |= CMD_MASTER;
    }
    pci_write16(d, PCI_COMMAND, cmd);
}

/* Message to this CPU's local APIC: fixed delivery, edge. */
int pci_enable_msi(pci_dev_t *d, uint8_t vector) {
    uint8_t c = d->msi_cap;
    uint16_t ctl;

    if (c == 0) {
        return -1;
    }
    ctl = pci_read16(d, (uint8_t)(c + 2));
    pci_write32(d, (uint8_t)(c + 4), 0xFEE00000u | (uint32_t)lapic_id() << 12);
    if (ctl & 0x80) { /* 64-bit address */
        pci_write32(d, (uint8_t)(c + 8), 0);
        pci_write16(d, (uint8_t)(c + 12), vector);
    } else {
        pci_write16(d, (uint8_t)(c + 8), vector);
    }
    pci_write16(d, (uint8_t)(c + 2), (uint16_t)((ctl & ~0x70u) | 1)); /* one vector, on */
    pci_write16(d, PCI_COMMAND, pci_read16(d, PCI_COMMAND) | CMD_INTX_OFF);
    return 0;
}
