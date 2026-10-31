| void call_native(struct regs *r, void (*fn)(void))   struct regs { uint32_t d[8]; uint32_t a[7]; uint32_t ccr; }  d0..d7 @0, a0..a6 @32, ccr @60
	.text
	.even
	.globl call_native
call_native:
	movem.l %d2-%d7/%a2-%a6,-(%sp)
	move.l 48(%sp),%a0
	move.l 52(%sp),%d0
	move.l %a0,-(%sp)
	pea after(%pc)
	move.l %d0,-(%sp)
	move.l 60(%a0),%d0
	move %d0,%ccr
	movem.l (%a0),%d0-%d7
	movem.l 32(%a0),%a0-%a6
	rts
after:
	move %ccr,-(%sp)
	move.l %d0,-(%sp)
	move.l %a0,-(%sp)
	move.l 10(%sp),%a0
	movem.l %d0-%d7,(%a0)
	movem.l %a1-%a6,36(%a0)
	move.l (%sp)+,32(%a0)
	addq.l #4,%sp
	move.w (%sp)+,%d0
	and.l #0xFFFF,%d0
	move.l %d0,60(%a0)
	addq.l #4,%sp
	movem.l (%sp)+,%d2-%d7/%a2-%a6
	rts
