global enter_user_mode

; enter_user_mode(uint64_t entry, uint64_t user_stack)
; rdi = ring-3 entry point (RIP)
; rsi = top of the ring-3 stack (RSP)
;
; Performs the one-time ring0 -> ring3 transition for a brand new user
; process's first run, by manually building an IRETQ frame and executing
; iretq. Does not return: control resumes in ring3 at `entry`. If that
; process is later preempted or makes a syscall, the existing
; isr_common_stub's own iretq (in isr.asm) handles resuming it -- this
; function is only needed for the very first switch-in.
enter_user_mode:
    mov ax, 0x23        ; user data selector (RPL=3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x23      ; SS (user data selector, RPL=3)
    push rsi             ; RSP (user stack top)
    pushfq
    pop rax
    or rax, 0x200        ; force IF=1 so interrupts fire in ring3
    push rax             ; RFLAGS
    push qword 0x1B       ; CS (user code selector, RPL=3)
    push rdi             ; RIP (entry point)
    iretq
