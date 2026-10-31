/* Linker script: ONE flat image linked at 0x800 (loaded by the bootblock), .bss/.chipbuf/.stack NOLOAD.
 *   0x000-0x7FF vectors + bootblock relocator;  0x800.. text rodata data;  then bss (ytab, 6809 image, renderer work, ...), stack, display buffers */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)
PROVIDE(_chip_limit = 0x1F0000);
PROVIDE(_stack_size = 0x1000);
SECTIONS
{
    . = 0x800;
    _payload_base = .;
    .text : { KEEP(*(.text.start)) *(.text .text.*) *(.rodata .rodata.*) }
    .data : ALIGN(4) { *(.data .data.*) . = ALIGN(4); }
    _load_end = .;
    .bss (NOLOAD) : ALIGN(4) { _bss_start = .; *(.bss .bss.* COMMON) . = ALIGN(4); _bss_end = .; }
    .stack (NOLOAD) : ALIGN(4) { . += _stack_size; _stack_top = .; }
    .chipbuf (NOLOAD) : ALIGN(16) { *(.chipbuf) . = ALIGN(4); }
    _end = .;
    ASSERT(_end <= _chip_limit, "image + buffers exceed chip RAM")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
