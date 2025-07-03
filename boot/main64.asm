global long_mode_start
extern kernel_main

section .text
bits 64
long_mode_start:
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

	call kernel_main
    hlt
