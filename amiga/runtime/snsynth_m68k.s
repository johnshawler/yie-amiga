| snsynth_m68k.s - the event loop of sns_render (snsynth.c) in 68020 assembly: uint32_t sns_core(struct core *r)
| Integrates the SN76489A output over the boxes of one frame, processing the toggles of the audible channels in time order, up to the next queued chip write (return 0, the
| core has integrated up to it: r->tw) or the end of the frame (return 1).  Writes, register decoding and everything rare stay in C.  Same arithmetic as the C loop in snsynth.c
| (the two produce bit-identical blocks: sound/sntest.c compares them).  struct core offsets: 0 A, 4 acc, 8 P0, 12 bi, 16 bn, 20 tw, 24 K, 28 out, 32 Aor, 36 rng, 40 white,
| 44 nt[4], 60 vol[4], 76 ch (chan_t: 32 bytes: 0 cnt, 4 per, 8 tref, 12 nxt, 16 out), 80 dit, 84 pat (32 x 16 bytes).
| Registers: d0 A, d1 acc, d2 P0, d3 bi, d5 = 22, d7 = channel index | 4 if a write is due; a5 out, a4 ch, a3 dit, a2 pat, a6 core; d4/d6/a0/a1 scratch.
	.text
	.even
	.globl	sns_core
sns_core:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a6
	move.l	0(%a6),%d0
	move.l	4(%a6),%d1
	move.l	8(%a6),%d2
	move.l	12(%a6),%d3
	move.l	28(%a6),%a5
	move.l	76(%a6),%a4
	move.l	80(%a6),%a3
	move.l	84(%a6),%a2
	moveq	#22,%d5
.Lit:
	move.l	44(%a6),%d4			| next toggle: m = min nt[], d7 = its channel
	moveq	#0,%d7
	cmp.l	48(%a6),%d4
	bls.s	1f
	move.l	48(%a6),%d4
	moveq	#1,%d7
1:	cmp.l	52(%a6),%d4
	bls.s	2f
	move.l	52(%a6),%d4
	moveq	#2,%d7
2:	cmp.l	56(%a6),%d4
	bls.s	3f
	move.l	56(%a6),%d4
	moveq	#3,%d7
3:	move.l	20(%a6),%d6			| te = min(tw, m); a write due at or before the toggle goes first
	cmp.l	%d4,%d6
	bhi.s	4f
	bset	#2,%d7
	bra.s	5f
4:	move.l	%d4,%d6
5:	movea.l	%d6,%a1				| a1 = te
	cmp.l	#12672,%d6
	bcc.s	6f
	move.l	24(%a6),%d4
	mulu.l	%d6,%d4				| P1 = te * K
	bra.s	7f
6:	move.l	16(%a6),%d4
	lsl.l	%d5,%d4				| P1 = bn << 22
7:	or.l	%d0,32(%a6)			| Aor |= A
	move.l	%d4,%d6
	lsr.l	%d5,%d6				| i1 = P1 >> 22
	cmp.l	%d3,%d6
	bne.s	10f
	move.l	%d4,%d6				| inside the current box: acc += A * ((P1 - P0) >> 6)
	sub.l	%d2,%d6
	lsr.l	#6,%d6
	mulu.l	%d0,%d6
	add.l	%d6,%d1
	move.l	%d4,%d2
	bra	.Lafter
10:	movea.l	%d7,%a0				| the segment crosses the end of box bi: finish it
	move.l	%d3,%d7
	addq.l	#1,%d7
	lsl.l	%d5,%d7
	sub.l	%d2,%d7
	lsr.l	#6,%d7
	mulu.l	%d0,%d7
	add.l	%d7,%d1
	move.l	%d3,%d7				| q = (acc + DIT[bi & 15]) >> 23, clamped, out[bi] = q - 128
	and.l	#15,%d7
	add.l	(%a3,%d7.l*4),%d1
	moveq	#23,%d7
	lsr.l	%d7,%d1
	cmp.l	#255,%d1
	bls.s	11f
	move.w	#255,%d1
11:	sub.b	#128,%d1
	move.b	%d1,(%a5,%d3.l)
	addq.l	#1,%d3
	cmp.l	%d3,%d6				| whole boxes: m = i1 - bi
	bls.s	20f
	move.l	%d6,%d7
	sub.l	%d3,%d7
	bsr	.Lfill
20:	move.l	%d4,%d1				| acc = A * ((P1 & 0x3FFFFF) >> 6); P0 = P1
	and.l	#0x3FFFFF,%d1
	lsr.l	#6,%d1
	mulu.l	%d0,%d1
	move.l	%d4,%d2
	move.l	%a0,%d7
.Lafter:
	cmpa.l	#12672,%a1
	bcc	.Lend
	btst	#2,%d7
	bne	.Lwrite
	| toggle of channel d7 at te
	move.l	%d7,%d6
	lsl.l	#5,%d6
	lea	(%a4,%d6.l),%a0			| a0 = &ch[d7]
	moveq	#3,%d6
	cmp.l	%d7,%d6
	beq.s	30f
	move.l	16(%a0),%d6			| tone: out ^= 1
	move.l	%d6,%d4
	eor.l	#1,%d4
	bra.s	33f
30:	move.l	36(%a6),%d6			| noise: one LFSR step (17 bits: new bit = bit2 [^ bit3 in white mode])
	move.l	%d6,%d4
	lsr.l	#2,%d4
	tst.l	40(%a6)
	beq.s	31f
	movea.l	%d6,%a1
	lsr.l	#3,%d6
	eor.l	%d6,%d4
	move.l	%a1,%d6
31:	and.l	#1,%d4
	lsr.l	#1,%d6
	swap	%d4
	or.l	%d4,%d6
	move.l	%d6,36(%a6)
	move.l	%d6,%d4
	and.l	#1,%d4
	move.l	16(%a0),%d6
33:	move.l	%d4,16(%a0)			| d6 = old output, d4 = new output
	cmp.l	%d6,%d4
	beq.s	34f
	move.l	(60,%a6,%d7.l*4),%d6
	tst.l	%d4
	beq.s	35f
	add.l	%d6,%d0
	bra.s	34f
35:	sub.l	%d6,%d0
34:	move.l	4(%a0),%d6			| cnt = per; tref = nxt + 1; nxt += max(per, 1); nt = 7 + 8 * nxt
	move.l	%d6,0(%a0)
	move.l	12(%a0),%d4
	addq.l	#1,%d4
	move.l	%d4,8(%a0)
	cmp.l	#1,%d6
	bge.s	36f
	moveq	#1,%d6
36:	add.l	%d6,%d4
	subq.l	#1,%d4
	move.l	%d4,12(%a0)
	lsl.l	#3,%d4
	addq.l	#7,%d4
	move.l	%d4,(44,%a6,%d7.l*4)
	bra	.Lit
.Lwrite:
	move.l	%a1,20(%a6)			| tw = te: the C side applies the write(s) due now
	moveq	#0,%d6
	bra.s	.Lout
.Lend:
	moveq	#1,%d6
.Lout:
	move.l	%d0,0(%a6)
	move.l	%d1,4(%a6)
	move.l	%d2,8(%a6)
	move.l	%d3,12(%a6)
	move.l	%d6,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| .Lfill: d7 = m >= 1 boxes of constant amplitude d0 starting at box d3 (out = a5): out[d3...] = quantised constant, d3 += m.  Clobbers d1, d6, d7.
| Pattern case: the carry pattern of F (16 bytes, stored twice: 32 bytes per F) is copied with the base added, in chunks of at most 16 boxes.
.Lfill:
	movem.l	%d0/%a0-%a2,-(%sp)
	move.l	%d0,%d1
	beq.s	.Lsil
	lsr.l	#7,%d1				| c = A >> 7
	cmp.l	#255,%d1
	bcc.s	.Lfull
	move.l	%d0,%d6
	lsr.l	#2,%d6
	and.l	#31,%d6
	lsl.l	#5,%d6
	lea	(%a2,%d6.l),%a2			| a2 = pattern of F
	sub.b	#128,%d1			| base = c - 128
	lea	(%a5,%d3.l),%a0			| output pointer
	move.l	%d3,%d6
	and.l	#15,%d6				| phase
	add.l	%d7,%d3				| bi += m
1:	lea	(%a2,%d6.l),%a1
	moveq	#16,%d0
	cmp.l	%d0,%d7
	bcc.s	2f
	move.l	%d7,%d0
2:	sub.l	%d0,%d7
	add.l	%d0,%d6
	and.l	#15,%d6
	subq.l	#1,%d0
3:	move.b	(%a1)+,(%a0)
	add.b	%d1,(%a0)+
	dbra	%d0,3b
	tst.l	%d7
	bne.s	1b
	bra.s	.Lfdone
.Lsil:	move.l	#0x80808080,%d1
	bra.s	.Lflat
.Lfull:	move.l	#0x7F7F7F7F,%d1
.Lflat:	lea	(%a5,%d3.l),%a0			| constant fill: bytes up to a long-word boundary, long words, bytes
	add.l	%d7,%d3
	move.l	%a0,%d6
	neg.l	%d6
	and.l	#3,%d6
	cmp.l	%d7,%d6
	bls.s	4f
	move.l	%d7,%d6
4:	sub.l	%d6,%d7
	subq.l	#1,%d6
	bmi.s	6f
5:	move.b	%d1,(%a0)+
	dbra	%d6,5b
6:	move.l	%d7,%d6
	lsr.l	#2,%d6
	and.l	#3,%d7
	subq.l	#1,%d6
	bmi.s	8f
7:	move.l	%d1,(%a0)+
	dbra	%d6,7b
8:	subq.l	#1,%d7
	bmi.s	.Lfdone
9:	move.b	%d1,(%a0)+
	dbra	%d7,9b
.Lfdone:
	movem.l	(%sp)+,%d0/%a0-%a2
	rts
