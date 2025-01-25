; idt.asm
section .text
global idt_load
extern idtp  ; Declare idtp as an external symbol

idt_load:
    ; Load the IDT pointer
    lidt [idtp]
    ret


