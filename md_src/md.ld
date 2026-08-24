OUTPUT_ARCH(m68k)
SEARCH_DIR(.)

MEMORY {
	rom (rx) : ORIGIN = 0x00880000, LENGTH = 4M
	ram (wx) : ORIGIN = 0x00FF0000, LENGTH = 64K
}

PROVIDE (__stack = 0x01000000);

SECTIONS {
	.text 0x008803F0 : {
		__text_start = .;
		*(.text.keepboot)
		*(.text .text.*)
		*(.rodata .rodata.*)
		. = ALIGN(2);   /* PAD THE SECTION, not just the symbol: .data's load
		                 * image sits at LOADADDR(.text)+SIZEOF(.text), and an
		                 * odd .text size put it at an odd ROM address — the
		                 * 68K's word-copy startup address-errored and the
		                 * whole cart booted black (2026-08-09, the SMS
		                 * payload's size flipped the parity). */
		__text_end = .;
	} > rom
	__text_size = SIZEOF(.text);

	.data 0x00FF0000 : AT(LOADADDR(.text) + SIZEOF(.text)) {
		__data_start = .;
		*(.data .data.*)
		__data_end = ALIGN(2);
	} > ram
	__data_size = SIZEOF(.data);

	.bss 0x00FF0000 + SIZEOF(.data) : {
		__bss_start = . ;
		*(.bss .bss.*)
		__bss_end = ALIGN(2);
	} > ram
	__bss_size = SIZEOF(.bss);
}
