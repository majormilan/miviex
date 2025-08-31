#include <kernel/hal/idt.h>
#include <kernel/libc/string.h>
#include <kernel/mm/memory.h>
#include <kernel/hal/isr.h>
#include <kernel/hal/io.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/syscall/syscall.h>

struct idt_entry idt[256];
struct idt_ptr idtp;

/*  External function to load the IDT, defined in Assembly */
extern void idt_load();

// Declare all 256 ISRs
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();
extern void isr32();
extern void isr33();
extern void isr34();
extern void isr35();
extern void isr36();
extern void isr37();
extern void isr38();
extern void isr39();
extern void isr40();
extern void isr41();
extern void isr42();
extern void isr43();
extern void isr44();
extern void isr45();
extern void isr46();
extern void isr47();
extern void isr128();

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_middle = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

/*  Function to initialize the IDT */
int init_idt() {
  idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
  idtp.base = (uint64_t)&idt;

  /*  Clear out the IDT, initialize to zero using the existing memset function */
  memset(&idt, 0, sizeof(struct idt_entry) * 256);

  // Set all 256 IDT entries
  idt_set_gate(0, (uint64_t)isr0, 0x08, 0x8E);
  idt_set_gate(1, (uint64_t)isr1, 0x08, 0x8E);
  idt_set_gate(2, (uint64_t)isr2, 0x08, 0x8E);
  idt_set_gate(3, (uint64_t)isr3, 0x08, 0x8E);
  idt_set_gate(4, (uint64_t)isr4, 0x08, 0x8E);
  idt_set_gate(5, (uint64_t)isr5, 0x08, 0x8E);
  idt_set_gate(6, (uint64_t)isr6, 0x08, 0x8E);
  idt_set_gate(7, (uint64_t)isr7, 0x08, 0x8E);
  idt_set_gate(8, (uint64_t)isr8, 0x08, 0x8E);
  idt_set_gate(9, (uint64_t)isr9, 0x08, 0x8E);
  idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E);
  idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E);
  idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E);
  idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E);
  idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E);
  idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E);
  idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E);
  idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E);
  idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E);
  idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E);
  idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E);
  idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E);
  idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E);
  idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E);
  idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E);
  idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E);
  idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E);
  idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E);
  idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E);
  idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E);
  idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E);
  idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E);

  // IRQs
  idt_set_gate(32, (uint64_t)isr32, 0x08, 0x8E);
  idt_set_gate(33, (uint64_t)isr33, 0x08, 0x8E);
  idt_set_gate(34, (uint64_t)isr34, 0x08, 0x8E);
  idt_set_gate(35, (uint64_t)isr35, 0x08, 0x8E);
  idt_set_gate(36, (uint64_t)isr36, 0x08, 0x8E);
  idt_set_gate(37, (uint64_t)isr37, 0x08, 0x8E);
  idt_set_gate(38, (uint64_t)isr38, 0x08, 0x8E);
  idt_set_gate(39, (uint64_t)isr39, 0x08, 0x8E);
  idt_set_gate(40, (uint64_t)isr40, 0x08, 0x8E);
  idt_set_gate(41, (uint64_t)isr41, 0x08, 0x8E);
  idt_set_gate(42, (uint64_t)isr42, 0x08, 0x8E);
  idt_set_gate(43, (uint64_t)isr43, 0x08, 0x8E);
  idt_set_gate(44, (uint64_t)isr44, 0x08, 0x8E);
  idt_set_gate(45, (uint64_t)isr45, 0x08, 0x8E);
  idt_set_gate(46, (uint64_t)isr46, 0x08, 0x8E);
  idt_set_gate(47, (uint64_t)isr47, 0x08, 0x8E);

  // Syscall
  idt_set_gate(128, (uint64_t)isr128, 0x08, 0x8E);

  /*  Load the IDT */
  idt_load();

  // Register default handlers for IRQs
  register_interrupt_handler(32, isr_timer_handler);
  register_interrupt_handler(33, keyboard_isr);
  return 0;
}