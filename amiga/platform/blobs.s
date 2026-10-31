	.section .rodata
	.balign 4
	.globl rom_blob, gfx_blob, vlm_blob
rom_blob:
	.incbin "ROMBLOB"
gfx_blob:
	.incbin "GFXBLOB"
vlm_blob:
	.incbin "VLMBLOB"
