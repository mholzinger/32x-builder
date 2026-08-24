OUTPUT_ARCH(sh)
SEARCH_DIR(.)

/* The memory map look like this:
 * +--------------------+ <- 0x02000000
 * | .text              |
 * |        _etext      |
 * |        ctor list   | the ctor and dtor lists are for
 * |        dtor list   | C++ support
 * +--------------------+
 * .                    .
 * .                    .
 * .                    .
 * +--------------------+ <- 0x06000000
 * | .data              | initialized data goes here
 * |        _edata      |
 * +--------------------+
 * | .bss               |
 * |        __bss_start | start of bss, cleared by crt0
 * |        _end        | start of heap, used by sbrk()
 * +--------------------+
 * .                    .
 * .                    .
 * .                    .
 * |        __stack     | top of stack (for Master SH2)
 * +--------------------+ <- 0x0603FC00
 */

MEMORY {
	rom (rx) : ORIGIN = 0x02000000, LENGTH = 0x00400000
	ram (wx) : ORIGIN = 0x06000000, LENGTH = 0x0003FC00
}

PROVIDE (__stack = 0x0603FC00);

SECTIONS {
	.text 0x02000000 : AT(0x00000000) {
		*(.text .text.*)
		. = ALIGN(0x4);

		__CTOR_LIST__ = .;
		___CTOR_LIST__ = .;
		LONG((__CTOR_END__ - __CTOR_LIST__) / 4 - 2)
		*(.ctors)
		LONG(0)
		__CTOR_END__ = .;
		__DTOR_LIST__ = .;
		___DTOR_LIST__ = .;
		LONG((__DTOR_END__ - __DTOR_LIST__) / 4 - 2)
		*(.dtors)
		LONG(0)
		__DTOR_END__ = .;
		*(.rodata .rodata.*)
		*(.gcc_except_table .gcc_except_table.*)

		. = ALIGN(0x10);
		__INIT_SECTION__ = . ;
		*(.init)
		SHORT (0x000B)	/* rts */
		SHORT (0x0009)	/* nop */

		. = ALIGN(0x10);
		__FINI_SECTION__ = . ;
		*(.fini)
		SHORT (0x000B)	/* rts */
		SHORT (0x0009)	/* nop */

		_etext = .;
		*(.lit)
		. = ALIGN(0x4);
	} > rom
	_stext = SIZEOF (.text);

	.data 0x06000000 : AT(LOADADDR(.text) + SIZEOF(.text)) {
		*(.got.plt) *(.got)
		*(.shdata)
		*(.data .data.*)
		_edata = .;
		. = ALIGN(0x4);
	} > ram

	/* Hot renderer code copied ROM->cacheable SDRAM at boot (d32xr Lever 1).
	 * Placed contiguously after .data, with its load image contiguous after
	 * .data's in ROM, so the MARS header's single ROM->SDRAM copy (Source
	 * _stext, Size _sdata) carries BOTH .data and .ramtext over — no crt0
	 * change. _sdata is therefore the combined size. Functions opt in via
	 * __attribute__((section(".ramtext"))). */
	.ramtext 0x06000000 + SIZEOF(.data) : AT(LOADADDR(.data) + SIZEOF(.data)) {
		. = ALIGN(0x4);
		*(.ramtext .ramtext.*)
		. = ALIGN(0x4);
	} > ram
	_sdata = SIZEOF(.data) + SIZEOF(.ramtext);

	.bss 0x06000000 + SIZEOF(.data) + SIZEOF(.ramtext) :
	{
		__bss_start = . ;
		*(.shbss)
		*(.bss .bss.*)
		*(COMMON)
		_end =  ALIGN (0x8);
		__end = _end;
		end = _end;
	} > ram

	/* MEMORY OVERLAY: buffers used ONLY at startup/menu and DEAD during gameplay,
	 * parked ABOVE _end (just under the Master stack at 0x0603F000). This keeps
	 * _end -- the protected .bss boundary the stack must never cross -- ~71 KB
	 * lower, so the render stack has ~75 KB of room; its worst-case spill lands in
	 * these dead buffers, never in live data. NOLOAD: not in ROM, not zero-inited
	 * by crt0 -- each buffer is scratch, filled before use.
	 *
	 * ORDER MATTERS. This region is also where the stack spills, so placement
	 * within it is ranked by "is this buffer live while the stack above it is
	 * deep?" .hero_overlay_lo goes FIRST (lowest, farthest from the stack): it
	 * holds buffers that are LIVE during a startup phase, so nothing may grow
	 * down into them. .hero_overlay goes LAST (highest, directly under the
	 * stack): only buffers dead during every phase whose stack could reach them.
	 * Getting this backwards froze the box intro -- box_cover sat 208 bytes
	 * under __stack while box3d was actively writing it, and the intro's ~1.5 KB
	 * stack ate its top rows along with the return addresses. */
	.hero_overlay (NOLOAD) :
	{
		. = ALIGN(0x8);
		*(.hero_overlay_lo)
		. = ALIGN(0x8);
		*(.hero_overlay)
	} > ram

	.eh_frame 0 (NOLOAD) :
	{
		*(.eh_frame)
	}
}
