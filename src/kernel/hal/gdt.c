#include <kernel/hal/gdt.h>
#include <stdint.h>

extern uint8_t tss[];

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
    // The GDT is already loaded by the bootloader.
    // We just need to initialize the TSS.
    struct tss_entry* tss_ptr = (struct tss_entry*)tss;
    tss_ptr->iopb_offset = sizeof(struct tss_entry);
    return 0;
}

void tss_set_stack(uint64_t stack) {
    struct tss_entry* tss_ptr = (struct tss_entry*)tss;
    tss_ptr->rsp0 = stack;
}
