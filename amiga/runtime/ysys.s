| ysys.s - glue between the translated 6809 blocks and the C runtime (68020).
| Register convention of translated code: see translator/emit.py.
	.include "yvm.inc"
	.text
	.even

	.globl ytab, ytab_mid, rt_vm, yolast, yolast_mid
	.set ytab_mid, ytab + 0x20000
	.set yolast_mid, yolast + 0x8000       | element for index 0; elements for sign-extended 16-bit 6809 addresses

| struct rtregs (native stack):  0 d0  4 d1  8 d2  12 d3  16 d4  20 ccr  24 a3  28 pc  32 s  36 force(in)/budget(out)  40 dp  44 cc_hi
	.set R_CCR, 20
	.set R_A3, 24
	.set R_PC, 28
	.set R_S, 32
	.set R_FORCE, 36
	.set R_DP, 40
	.set R_CCH, 44
	.set R_SIZE, 48

| rt_refill: the block budget (d7.w) ran out at block `a0` (sign-extended 6809 address).  Flags are live.
	.globl rt_refill
rt_refill:
	move	%ccr,%d6
	cmp.l	#128,%a3
	blt.s	tail
	move.l	%a3,%d5
	lsr.l	#6,%d5
	subq.l	#1,%d5
	cmp.l	#0xFFFF,%d5
	bls.s	1f
	move.l	#0xFFFF,%d5
1:	move.w	%d5,%d7
	move	%d6,%ccr
	jmp	([ytab_mid,%za0,%a0.l*4])
tail:					| less than 128 cycles to the event: run this block natively iff its last instruction starts before the event
	moveq	#0,%d5
	move.b	(yolast_mid,%za0,%a0.l*1),%d5
	cmp.l	%d5,%a3
	ble.s	slow_noforce
	moveq	#1,%d7			| budget for exactly one block: the next block entry comes back here
	move	%d6,%ccr
	jmp	([ytab_mid,%za0,%a0.l*4])

slow_noforce:
	moveq	#0,%d5
	bra.s	slow_common
	.globl rt_interp1
rt_interp1:
	move	%ccr,%d6
	.globl rt_interp1_d6
rt_interp1_d6:				| entry with the flags already in d6
	moveq	#1,%d5
slow_common:				| d5 = force flag, d6 = ccr, a0 = pc (sign-extended word)
	lea	-R_SIZE(%sp),%sp
	movem.l	%d0-%d4,0(%sp)
	and.l	#0x1F,%d6
	move.l	%d6,R_CCR(%sp)
	move.l	%a3,R_A3(%sp)
	move.w	%a0,%d6
	and.l	#0xFFFF,%d6
	move.l	%d6,R_PC(%sp)
	move.l	%a4,%d6
	sub.l	%a1,%d6
	move.l	%d6,R_S(%sp)
	move.l	%d5,R_FORCE(%sp)
	moveq	#0,%d6
	move.b	vm_dp(%a6),%d6
	move.l	%d6,R_DP(%sp)
	move.b	vm_cc(%a6),%d6
	and.l	#0xFF,%d6
	move.l	%d6,R_CCH(%sp)
	move.l	%sp,-(%sp)
	jsr	rt_slow_c
	addq.l	#4,%sp
rt_load_state:				| (sp) -> struct rtregs
	move.l	image_base,%a1			| a1 is a scratch register for the C code
	movem.l	0(%sp),%d0-%d4
	move.l	R_A3(%sp),%a3
	move.l	R_S(%sp),%d5
	lea	0(%a1,%d5.l),%a4
	move.l	R_DP(%sp),%d5
	move.b	%d5,vm_dp(%a6)
	lsl.w	#8,%d5
	and.l	#0xFFFF,%d5
	lea	0(%a1,%d5.l),%a2
	move.l	R_CCH(%sp),%d5
	move.b	%d5,vm_cc(%a6)
	move.l	R_FORCE(%sp),%d7		| new block budget
	movea.w	R_PC+2(%sp),%a0
	move.l	R_CCR(%sp),%d6
	lea	R_SIZE(%sp),%sp
	move	%d6,%ccr
	jmp	([ytab_mid,%za0,%a0.l*4])

| rt_enter(struct rtregs *r): load the translated-code registers from r and jump into the code; never returns.
	.globl rt_enter
rt_enter:
	move.l	4(%sp),%a0
	lea	rt_vm,%a6
	move.l	image_base,%a1
	lea	0x8000(%a1),%a5
	lea	-R_SIZE(%sp),%sp
	move.l	%sp,%a2
	moveq	#(R_SIZE/4)-1,%d0
1:	move.l	(%a0)+,(%a2)+
	dbra	%d0,1b
	bra	rt_load_state

	.globl image_base
	.comm image_base, 4

| I/O: a0 = address (word), (sp) -> .word cycles from the instruction start to the block end;  rt_io_rd returns d5.b, rt_io_wr takes d5.b
	.globl rt_io_rd, rt_io_wr, rt_wr_4000
rt_wr_4000:				| d5.b = value written to the control port
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	and.l	#0xFF,%d5
	move.l	%d5,-(%sp)
	jsr	board_wr_ctrl
	addq.l	#4,%sp
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	rts
rt_io_rd:
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	move.l	16(%sp),%a1
	moveq	#0,%d1
	move.w	(%a1),%d1
	addq.l	#2,16(%sp)
	move.l	%d1,-(%sp)
	move.l	%a3,-(%sp)
	move.w	%a0,%d0
	and.l	#0xFFFF,%d0
	move.l	%d0,-(%sp)
	jsr	rt_io_rd_c
	lea	12(%sp),%sp
	move.b	%d0,%d5
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	rts
rt_io_wr:
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	move.l	16(%sp),%a1
	moveq	#0,%d1
	move.w	(%a1),%d1
	addq.l	#2,16(%sp)
	move.l	%d1,-(%sp)
	move.l	%a3,-(%sp)
	and.l	#0xFF,%d5
	move.l	%d5,-(%sp)
	move.w	%a0,%d0
	and.l	#0xFFFF,%d0
	move.l	%d0,-(%sp)
	jsr	rt_io_wr_c
	lea	16(%sp),%sp
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	rts

| helpers executed by the 6809 core on a scratch CPU:  d0=A d1=B d2=X d5=operand byte d6=old flags (CCR copy)
	.macro HELPER name, opcode
	.globl rt_h_\name
rt_h_\name:
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	lea	-12(%sp),%sp
	move.b	%d0,0(%sp)
	move.b	%d1,1(%sp)
	move.w	%d2,2(%sp)
	move.b	%d6,%d0
	and.b	#0x0F,%d0
	or.b	vm_cc(%a6),%d0
	move.b	%d0,4(%sp)
	move.b	#\opcode,5(%sp)
	move.b	%d5,6(%sp)
	move.l	%sp,-(%sp)
	jsr	rt_helper_c
	addq.l	#4,%sp
	move.b	4(%sp),%d6
	move.b	%d6,%d5
	and.b	#0xF0,%d5
	move.b	%d5,vm_cc(%a6)
	and.w	#0x0F,%d6
	move.b	0(%sp),%d0
	move.b	1(%sp),%d1
	move.w	2(%sp),%d2
	lea	12(%sp),%sp
	addq.l	#8,%sp
	movem.l	(%sp)+,%a0-%a1
	move	%d6,%ccr
	rts
	.endm
	HELPER adca, 0x89
	HELPER adcb, 0xC9
	HELPER sbca, 0x82
	HELPER sbcb, 0xC2
	HELPER rola, 0x49
	HELPER rolb, 0x59
	HELPER rora, 0x46
	HELPER rorb, 0x56
	HELPER daa, 0x19
	HELPER mul, 0x3D
	HELPER sex, 0x1D
	HELPER abx, 0x3A

| debug: effective address check (a0 = host address of a dynamic access), (sp) -> .word pc
	.globl rt_chk
rt_chk:
	move	%ccr,-(%sp)
	movem.l	%d0-%d1/%a0-%a1,-(%sp)
	move.l	%a0,%d0
	sub.l	image_base,%d0
	move.l	18(%sp),%a1
	moveq	#0,%d1
	move.w	(%a1),%d1
	addq.l	#2,18(%sp)
	move.l	%d1,-(%sp)
	move.l	%d0,-(%sp)
	jsr	rt_chk_c
	addq.l	#8,%sp
	movem.l	(%sp)+,%d0-%d1/%a0-%a1
	move	(%sp)+,%ccr
	rts

	.globl rt_trap_data
rt_trap_data:
	jsr	rt_trap_c

	.bss
	.even
ytab:	.space 0x40000
yolast:	.space 0x10000
rt_vm:	.space 16
