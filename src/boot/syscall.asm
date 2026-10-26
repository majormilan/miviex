global syscall_entry
extern syscall_dispatch
extern tss

; Linux x86-64 syscall ABI: rax = number, args in rdi, rsi, rdx, r10, r8, r9,
; result in rax. The CPU leaves the return RIP in rcx and RFLAGS in r11 and
; masks IF via SFMASK, so nothing can interrupt us before the stack switch.
;
; Single CPU for now: the kernel stack comes straight from TSS.RSP0, which
; the scheduler keeps pointed at the current process. SMP needs swapgs and
; per-CPU data here.
;
; The frame built below is a registers_t, identical to isr_common_stub's,
; so C code sees the same layout for interrupts and syscalls.
section .text
syscall_entry:
    mov [user_rsp], rsp
    mov rsp, [tss + 4]      ; TSS.RSP0

    push qword 0x1B         ; ss
    push qword [user_rsp]   ; rsp
    push r11                ; rflags
    push qword 0x23         ; cs
    push rcx                ; rip
    push 0                  ; err_code
    push 0                  ; int_no

    push rax
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

    mov rdi, rsp
    call syscall_dispatch

    pop r15
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

    add rsp, 16             ; int_no, err_code
    pop rcx                 ; rip
    add rsp, 8              ; cs
    pop r11                 ; rflags
    pop rsp                 ; user rsp
    o64 sysret

section .bss
user_rsp: resq 1
