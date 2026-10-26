ENTRY(start)

/* Virtual = physical + KERNEL_BASE for everything but the boot stub. */
KERNEL_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
	. = 1M;

	/* Whole loaded kernel image (virtual): used by PMM to reserve frames. */
	_kernel_start = . + KERNEL_BASE;

	.boot :
	{
		KEEP(*(.multiboot_header))
		*(.boot.text)
	}

	. += KERNEL_BASE;

	.text ALIGN(4K) : AT(ADDR(.text) - KERNEL_BASE)
	{
		_text_start = .;
		*(.text .text.*)
		_text_end = .;
	}

	.rodata ALIGN(4K) : AT(ADDR(.rodata) - KERNEL_BASE)
	{
		_rodata_start = .;
		*(.rodata .rodata.*)
		_rodata_end = .;
	}

	.data ALIGN(4K) : AT(ADDR(.data) - KERNEL_BASE)
	{
		_data_start = .;
		*(.data .data.*)
		_data_end = .;
	}

	.bss ALIGN(4K) : AT(ADDR(.bss) - KERNEL_BASE)
	{
		__bss_start = .;
		*(COMMON)
		*(.bss .bss.*)
		. = ALIGN(4K);
		__bss_end = .;
	}

	/* First byte past the kernel image (page-aligned via .bss above). */
	_kernel_end = .;

	/DISCARD/ : { *(.eh_frame) *(.comment) *(.note .note.*) }
}
