#include <kernel/hal/acpi.h>
#include <kernel/hal/io.h>
#include <kernel/hal/multiboot.h>
#include <kernel/mm/memory.h>
#include <kernel/mm/vmm.h>
#include <kernel/libc/string.h>
#include <kernel/log.h>

/*
 * ACPI tables, read once at boot: MADT (interrupt controllers), FADT
 * (power and reset), HPET and MCFG. No AML interpreter: the \_S5 sleep
 * values for poweroff come from a byte scan of the DSDT, which is what
 * the hypervisors' firmware makes easy.
 */

acpi_info_t acpi;

typedef struct {
    char sig[4];
    uint32_t length;
    uint8_t revision, checksum;
    char oem[6], oem_table[8];
    uint32_t oem_rev, creator, creator_rev;
} __attribute__((packed)) sdt_t;

typedef struct {
    char sig[8];
    uint8_t checksum;
    char oem[6];
    uint8_t revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
    uint8_t xchecksum, reserved[3];
} __attribute__((packed)) rsdp_t;

static const sdt_t *tables[32];
static uint32_t ntables;

/* FADT power and reset data */
static uint32_t smi_cmd, pm1a_cnt, pm1b_cnt;
static uint8_t acpi_enable_val;
static uint16_t slp_typa, slp_typb;
static bool have_s5, have_reset;
static uint8_t reset_space, reset_value;
static uint64_t reset_addr;

static bool checksum_ok(const void *p, size_t len) {
    uint8_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum += ((const uint8_t *)p)[i];
    }
    return sum == 0;
}

/* Map a whole table: its header first, to learn the length. */
static const sdt_t *map_table(uint64_t phys) {
    const sdt_t *h = vmm_ioremap(phys, sizeof(sdt_t), false);

    if (h == NULL || h->length < sizeof(sdt_t) || h->length > 1024 * 1024) {
        return NULL;
    }
    h = vmm_ioremap(phys, h->length, false);
    return h != NULL && checksum_ok(h, h->length) ? h : NULL;
}

static const rsdp_t *find_rsdp(void) {
    const rsdp_t *r = get_multiboot_rsdp();

    if (r != NULL) {
        return r;
    }
    /* Legacy BIOS: 16-byte aligned in the first KiB of the EBDA or the
     * BIOS area below 1 MiB, which the direct map covers. */
    uint64_t ebda = (uint64_t)*(uint16_t *)phys_to_virt(0x40E) << 4;
    uint64_t areas[2][2] = { { ebda, ebda + 1024 }, { 0xE0000, 0x100000 } };
    for (int a = 0; a < 2; a++) {
        for (uint64_t p = areas[a][0]; p + 20 <= areas[a][1]; p += 16) {
            r = phys_to_virt(p);
            if (memcmp(r->sig, "RSD PTR ", 8) == 0 && checksum_ok(r, 20)) {
                return r;
            }
        }
    }
    return NULL;
}

const void *acpi_table(const char sig[4]) {
    for (uint32_t i = 0; i < ntables; i++) {
        if (memcmp(tables[i]->sig, sig, 4) == 0) {
            return tables[i];
        }
    }
    return NULL;
}

static void parse_madt(const sdt_t *t) {
    const uint8_t *p = (const uint8_t *)t + 44, *end = (const uint8_t *)t + t->length;

    acpi.lapic = *(const uint32_t *)((const uint8_t *)t + 36);
    for (; p + 2 <= end && p[1] >= 2; p += p[1]) {
        switch (p[0]) {
        case 0: /* processor local APIC: enabled or online-capable */
            if (*(const uint32_t *)(p + 4) & 3) {
                acpi.ncpus++;
            }
            break;
        case 1: /* I/O APIC */
            if (acpi.nioapic < ACPI_MAX_IOAPICS) {
                acpi.ioapic[acpi.nioapic].id = p[2];
                acpi.ioapic[acpi.nioapic].addr = *(const uint32_t *)(p + 4);
                acpi.ioapic[acpi.nioapic].gsi_base = *(const uint32_t *)(p + 8);
                acpi.nioapic++;
            }
            break;
        case 2: /* interrupt source override: ISA IRQ -> GSI */
            if (p[2] == 0 && p[3] < 16) {
                acpi.isa_gsi[p[3]] = *(const uint32_t *)(p + 4);
                acpi.isa_flags[p[3]] = *(const uint16_t *)(p + 8);
            }
            break;
        case 5: /* 64-bit local APIC address */
            acpi.lapic = *(const uint64_t *)(p + 4);
            break;
        }
    }
}

/* \_S5 is "Name(_S5_, Package() { SLP_TYPa, SLP_TYPb, ... })". */
static void find_s5(const uint8_t *aml, uint32_t len) {
    for (uint32_t i = 2; i + 8 < len; i++) {
        const uint8_t *p = aml + i;
        if (memcmp(p, "_S5_", 4) != 0 || p[4] != 0x12 ||
            !(p[-1] == 0x08 || (p[-2] == 0x08 && p[-1] == '\\'))) {
            continue;
        }
        p += 5;
        p += ((*p & 0xC0) >> 6) + 1; /* PkgLength */
        p++;                         /* NumElements */
        uint16_t v[2];
        for (int k = 0; k < 2; k++) {
            if (*p == 0x0A) {        /* BytePrefix */
                p++;
            }
            v[k] = *p++;
        }
        slp_typa = v[0];
        slp_typb = v[1];
        have_s5 = true;
        return;
    }
}

static void parse_fadt(const sdt_t *t) {
    const uint8_t *f = (const uint8_t *)t;
    uint64_t dsdt = *(const uint32_t *)(f + 40);
    const sdt_t *d;

    smi_cmd = *(const uint32_t *)(f + 48);
    acpi_enable_val = f[52];
    pm1a_cnt = *(const uint32_t *)(f + 64);
    pm1b_cnt = *(const uint32_t *)(f + 68);
    acpi.century = t->length > 108 ? f[108] : 0;
    if (t->length >= 129 && (*(const uint32_t *)(f + 112) & (1 << 10))) { /* RESET_REG_SUP */
        reset_space = f[116];
        reset_addr = *(const uint64_t *)(f + 120);
        reset_value = f[128];
        have_reset = true;
    }
    if (t->length >= 148 && *(const uint64_t *)(f + 140) != 0) {
        dsdt = *(const uint64_t *)(f + 140);
    }
    if (dsdt != 0 && (d = map_table(dsdt)) != NULL) {
        find_s5((const uint8_t *)d + sizeof(sdt_t), d->length - sizeof(sdt_t));
    }
}

void acpi_init(void) {
    const rsdp_t *r = find_rsdp();
    const sdt_t *root;
    bool x;

    for (int i = 0; i < 16; i++) {
        acpi.isa_gsi[i] = (uint32_t)i;
    }
    if (r == NULL) {
        klog(LOG_INFO, "acpi", "No RSDP: legacy PC configuration");
        return;
    }
    x = r->revision >= 2 && r->xsdt != 0;
    root = map_table(x ? r->xsdt : r->rsdt);
    if (root == NULL) {
        klog(LOG_FAIL, "acpi", "Bad %s", x ? "XSDT" : "RSDT");
        return;
    }
    uint32_t n = (root->length - sizeof(sdt_t)) / (x ? 8 : 4);
    for (uint32_t i = 0; i < n && ntables < 32; i++) {
        const uint8_t *e = (const uint8_t *)root + sizeof(sdt_t) + i * (x ? 8 : 4);
        uint64_t phys = x ? *(const uint64_t *)e : *(const uint32_t *)e;
        const sdt_t *t = map_table(phys);
        if (t != NULL) {
            tables[ntables++] = t;
        }
    }
    acpi.present = true;

    const sdt_t *t;
    if ((t = acpi_table("APIC")) != NULL) {
        parse_madt(t);
    }
    if ((t = acpi_table("FACP")) != NULL) {
        parse_fadt(t);
    }
    acpi.hpet = (uint64_t)(uintptr_t)acpi_table("HPET");
    acpi.mcfg = (uint64_t)(uintptr_t)acpi_table("MCFG");

    char sigs[32 * 5 + 1];
    for (uint32_t i = 0; i < ntables; i++) {
        memcpy(sigs + i * 5, tables[i]->sig, 4);
        sigs[i * 5 + 4] = ' ';
    }
    sigs[ntables * 5] = '\0';
    klog(LOG_OK, "acpi", "Rev %d, tables: %s", r->revision, sigs);
    klog(LOG_OK, "acpi", "%u CPU(s), %u I/O APIC(s), LAPIC at 0x%lx, S5 %s, reset %s",
         acpi.ncpus, acpi.nioapic, acpi.lapic, have_s5 ? "yes" : "no", have_reset ? "yes" : "no");
}

/* ACPI mode on, then SLP_TYP for S5 with SLP_EN. */
void acpi_poweroff(void) {
    if (!have_s5 || pm1a_cnt == 0) {
        return;
    }
    if (!(inw((uint16_t)pm1a_cnt) & 1) && smi_cmd != 0 && acpi_enable_val != 0) {
        outb((uint16_t)smi_cmd, acpi_enable_val);
        for (int i = 0; i < 300000 && !(inw((uint16_t)pm1a_cnt) & 1); i++) {
            inb(0x80);
        }
    }
    outw((uint16_t)pm1a_cnt, (uint16_t)(slp_typa << 10 | 1 << 13));
    if (pm1b_cnt != 0) {
        outw((uint16_t)pm1b_cnt, (uint16_t)(slp_typb << 10 | 1 << 13));
    }
    for (int i = 0; i < 200000; i++) {
        inb(0x80);
    }
}

void acpi_reset(void) {
    if (!have_reset) {
        return;
    }
    if (reset_space == 1) {          /* system I/O */
        outb((uint16_t)reset_addr, reset_value);
    } else if (reset_space == 0) {   /* system memory */
        volatile uint8_t *r = vmm_ioremap(reset_addr, 1, true);
        if (r != NULL) {
            *r = reset_value;
        }
    } else if (reset_space == 2) {   /* PCI config space of bus 0 */
        uint32_t dev = (uint32_t)(reset_addr >> 32) & 0xFFFF, fn = (uint32_t)(reset_addr >> 16) & 0xFFFF;
        uint32_t off = (uint32_t)reset_addr & 0xFFFF;
        outl(0xCF8, 0x80000000u | dev << 11 | fn << 8 | (off & 0xFC));
        outb((uint16_t)(0xCFC + (off & 3)), reset_value);
    }
    for (int i = 0; i < 200000; i++) {
        inb(0x80);
    }
}
