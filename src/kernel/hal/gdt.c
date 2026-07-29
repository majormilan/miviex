#include <kernel/hal/gdt.h>
#include <stdint.h>

extern uint8_t tss[];
extern uint8_t gdt_tss[]; // The TSS descriptor entry itself, in gdt.asm's GDT

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

int gdt_init() {
    // gdt.asm hardcodes the TSS descriptor's base address fields as 0
    // (the real address of `tss[]` isn't known until link time), so the
    // CPU would otherwise read TSS fields (like RSP0, consulted on every
    // ring3->ring0 transition) starting at physical address 0 instead of
    // the actual tss[] buffer. Patch the descriptor's base fields with the
    // real address now, then reload TR so the CPU's cached TSS base/limit
    // shadow registers pick up the fix (the bootloader already did an
    // initial `ltr` with the stale base baked in).
    uint64_t base = (uint64_t)tss;
    gdt_tss[2] = (uint8_t)(base & 0xFF);
    gdt_tss[3] = (uint8_t)((base >> 8) & 0xFF);
    gdt_tss[4] = (uint8_t)((base >> 16) & 0xFF);
    gdt_tss[7] = (uint8_t)((base >> 24) & 0xFF);
    gdt_tss[8] = (uint8_t)((base >> 32) & 0xFF);
    gdt_tss[9] = (uint8_t)((base >> 40) & 0xFF);
    gdt_tss[10] = (uint8_t)((base >> 48) & 0xFF);
    gdt_tss[11] = (uint8_t)((base >> 56) & 0xFF);

    // The bootloader's earlier `ltr` already marked this descriptor "busy"
    // (type 0xB instead of 0x9); loading TR again from a busy TSS
    // descriptor causes a #GP, so clear the busy bit first.
    gdt_tss[5] &= (uint8_t)~0x02;

    uint16_t tss_sel = TSS_SEL;
    asm volatile("ltr %0" :: "r"(tss_sel));

    struct tss_entry* tss_ptr = (struct tss_entry*)tss;
    tss_ptr->iopb_offset = sizeof(struct tss_entry);
    return 0;
}

void tss_set_stack(uint64_t stack) {
    struct tss_entry* tss_ptr = (struct tss_entry*)tss;
    tss_ptr->rsp0 = stack;
}
