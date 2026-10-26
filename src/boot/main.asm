global start
global stack_top
extern long_mode_start
%include "gdt.asm"

; The kernel is linked at KERNEL_BASE + phys. This 32-bit stub runs before
; paging, so it is linked low (.boot.text) and reaches every high symbol
; through its physical address, sym - KERNEL_BASE.
section .boot.text progbits alloc exec
bits 32

start:
	mov edi, ebx ; Multiboot info pointer; cpuid below clobbers ebx
	mov esp, stack_top - KERNEL_BASE

	call check_multiboot
	call check_cpuid
	call check_long_mode

	call setup_page_tables
	call enable_paging

	lgdt [gdt64_pointer_phys - KERNEL_BASE]

	jmp 0x08:long_mode_start ; GDT_CODE_KERNEL

	hlt

check_multiboot:
	cmp eax, 0x36d76289
	jne .no_multiboot
	ret
.no_multiboot:
	mov al, "M"
	jmp error

check_cpuid:
	pushfd
	pop eax
	mov ecx, eax
	xor eax, 1 << 21
	push eax
	popfd
	pushfd
	pop eax
	push ecx
	popfd
	cmp eax, ecx
	je .no_cpuid
	ret
.no_cpuid:
	mov al, "C"
	jmp error

check_long_mode:
	mov eax, 0x80000000
	cpuid
	cmp eax, 0x80000001
	jb .no_long_mode

	mov eax, 0x80000001
	cpuid
	test edx, 1 << 29
	jz .no_long_mode
	
	ret
.no_long_mode:
	mov al, "L"
	jmp error

setup_page_tables:
	; PML4[0]   -> l3     : identity map of the low 1 GiB (boot only)
	; PML4[511] -> l3_high: same 1 GiB at KERNEL_BASE (-2 GiB, PML3[510])
	mov eax, page_table_l3 - KERNEL_BASE
	or eax, 0b11 ; present, writable
	mov [page_table_l4 - KERNEL_BASE], eax

	mov eax, page_table_l3_high - KERNEL_BASE
	or eax, 0b11
	mov [page_table_l4 - KERNEL_BASE + 511 * 8], eax

	mov eax, page_table_l2 - KERNEL_BASE
	or eax, 0b11
	mov [page_table_l3 - KERNEL_BASE], eax
	mov [page_table_l3_high - KERNEL_BASE + 510 * 8], eax

	mov ecx, 0 ; counter
.loop:

	mov eax, 0x200000 ; 2MiB
	mul ecx
	or eax, 0b10000011 ; present, writable, huge page
	mov [page_table_l2 - KERNEL_BASE + ecx * 8], eax

	inc ecx ; increment counter
	cmp ecx, 512 ; checks if the whole table is mapped
	jne .loop ; if not, continue

	ret

enable_paging:
	; pass page table location to cpu
	mov eax, page_table_l4 - KERNEL_BASE
	mov cr3, eax

	; enable PAE
	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	; enable long mode
	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	; enable paging
	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax

	ret

error:
	; print "ERR: X" where X is the error code
	mov dword [0xb8000], 0x4f524f45
	mov dword [0xb8004], 0x4f3a4f52
	mov dword [0xb8008], 0x4f204f20
	mov byte  [0xb800a], al
	hlt

section .bss
align 4096
global page_table_l4
page_table_l4:
	resb 4096
global page_table_l3
page_table_l3:
	resb 4096
global page_table_l2
page_table_l2:
	resb 4096
page_table_l3_high:
	resb 4096
stack_bottom:
	resb 4096 * 4
stack_top:

global tss
tss:
    resb 104
