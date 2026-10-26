global long_mode_start
extern kernel_main
extern gdt64_pointer
extern stack_top

; Entered from the 32-bit stub through a far jump, which can only reach a
; low address. Hop to the higher half, then drop the boot-time identity
; addresses: high stack, GDT reloaded through its high alias.
section .boot.text
bits 64
long_mode_start:
    mov rax, higher_half
    jmp rax

section .text
higher_half:
    mov rsp, stack_top
    lgdt [rel gdt64_pointer]

    ; enable fpu/sse
    mov rax, cr0
    and ax, 0xFFFB
    or ax, 0x2
    mov cr0, rax
    mov rax, cr4
    or rax, 0x600
    mov cr4, rax

    ; load null into all data segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov edi, edi ; zero-extend: upper halves are undefined after the mode switch
    call kernel_main
    hlt
