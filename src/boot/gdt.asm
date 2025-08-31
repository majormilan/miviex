
section .rodata

gdt64:
    dq 0 ; Null descriptor

; Kernel Code Segment
gdt_code_kernel:
    dw 0xFFFF       ; Limit (low)
    dw 0            ; Base (low)
    db 0            ; Base (mid)
    db 0x9A         ; Access byte: P=1, DPL=0, S=1, Type=10 (code, execute/read)
    db 0xAF         ; Granularity: G=1, D=1, L=1, Limit (high)
    db 0            ; Base (high)

; Kernel Data Segment
gdt_data_kernel:
    dw 0xFFFF       ; Limit (low)
    dw 0            ; Base (low)
    db 0            ; Base (mid)
    db 0x92         ; Access byte: P=1, DPL=0, S=1, Type=2 (data, read/write)
    db 0xCF         ; Granularity: G=1, D=1, L=0, Limit (high)
    db 0            ; Base (high)

; User Code Segment
gdt_code_user:
    dw 0xFFFF       ; Limit (low)
    dw 0            ; Base (low)
    db 0            ; Base (mid)
    db 0xFA         ; Access byte: P=1, DPL=3, S=1, Type=10 (code, execute/read)
    db 0xAF         ; Granularity: G=1, D=1, L=1, Limit (high)
    db 0            ; Base (high)

; User Data Segment
gdt_data_user:
    dw 0xFFFF       ; Limit (low)
    dw 0            ; Base (low)
    db 0            ; Base (mid)
    db 0xF2         ; Access byte: P=1, DPL=3, S=1, Type=2 (data, read/write)
    db 0xCF         ; Granularity: G=1, D=1, L=0, Limit (high)
    db 0            ; Base (high)

; TSS Segment
gdt_tss:
    dw 104          ; Limit (low)
    dw 0            ; Base (low)
    db 0            ; Base (mid)
    db 0x89         ; Access byte: P=1, DPL=0, S=0, Type=9 (TSS)
    db 0x40         ; Granularity: G=0, D=0, L=0, Limit (high)
    db 0            ; Base (high)
    dq 0            ; Base (upper) and reserved

gdt64_pointer:
    dw $ - gdt64 - 1 ; GDT limit
    dq gdt64        ; GDT base

section .bss
global tss
tss:
    resb 104
