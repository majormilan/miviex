global context_switch

extern tss_set_stack

; context_switch(old_process_ptr, new_process_ptr)
; rdi = old_process_ptr
; rsi = new_process_ptr
context_switch:
    ; Calculate offset to registers_t within process_t
    %define PROCESS_REGISTERS_OFFSET 8

    ; Load RIP and jump
    mov rax, qword [rsi + PROCESS_REGISTERS_OFFSET + 136] ; Load RIP
    jmp rax