section .bss
align 16
stack_bottom:
    resb 4096
stack_top:

section .data
; Define an IDT pointer
extern idtp

section .text
global idt_load

idt_load:
    ; Load the IDT pointer
    lidt [idtp]
    ret
