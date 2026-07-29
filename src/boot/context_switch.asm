global context_switch

; context_switch(uint64_t *old_rsp, uint64_t new_rsp)
; rdi = pointer to where the *current* context's stack pointer is saved
; rsi = stack pointer to switch to (a value, not a pointer)
;
; Saves the callee-saved registers (per the SysV AMD64 ABI: rbp, rbx, r12-r15)
; and RFLAGS of the currently running context onto its own stack, stores the
; resulting RSP at *old_rsp, then switches to the new_rsp stack and restores
; the same set of registers/RFLAGS from there before returning via `ret`.
;
; For a process that has never run before, process_create() synthesizes a
; matching stack frame so this `ret` lands directly in process_trampoline().
context_switch:
    pushfq
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp   ; save outgoing stack pointer
    mov rsp, rsi      ; switch to the incoming stack

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    popfq

    ret