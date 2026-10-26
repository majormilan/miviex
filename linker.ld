ENTRY(start)

SECTIONS
{
	. = 1M;

	/* Whole loaded kernel image: used by PMM to reserve frames. */
	_kernel_start = .;

	.boot :
	{
		KEEP(*(.multiboot_header))
	}

	.text ALIGN(4K) :
	{
		_text_start = .;
		*(.text)
		_text_end = .;
	}

	.rodata ALIGN(4K) :
	{
		_rodata_start = .;
		*(.rodata)
		_rodata_end = .;
	}

	.data ALIGN(4K) :
	{
		_data_start = .;
		*(.data)
		_data_end = .;
	}

	.bss ALIGN(4K) :
	{
		__bss_start = .;
		*(.bss)
		. = ALIGN(4K);
		__bss_end = .;
	}

	/* First byte past the kernel image (page-aligned via .bss above). */
	_kernel_end = .;
}