section .text
global isr0
extern isr0_handler

isr0:
    push rax                   ; Save the general-purpose registers
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    call isr0_handler          ; Call the C handler

    pop r15                    ; Restore the general-purpose registers
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    iretq                      ; Return from interrupt
