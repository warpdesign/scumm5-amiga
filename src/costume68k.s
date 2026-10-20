| scumm5-amiga - costume cel decoding and scratch flush for the 68020
| SPDX-License-Identifier: GPL-3.0-or-later
|
| C models: cost_runs and the planar_put8 loop in src/scumm/costume.c.
| Loops are kept small enough for the 256-byte instruction cache.

	.text

| struct cost_runs offsets
	.set	R_SRC, 0
	.set	R_SP, 4
	.set	R_WP, 8
	.set	R_ZM, 12
	.set	R_CM, 16
	.set	R_TAB, 20
	.set	R_LEN, 24
	.set	R_PC, 28
	.set	R_TRANSP, 32
	.set	R_HEIGHT, 36
	.set	R_COLUMNS, 40
	.set	R_MODE, 44
	.set	R_MASKBIT, 48

| registers while decoding:
|  d0 run length left  d1 rows left in column  d2 colour  d3 mask bit
|  d4 height (bit 31: columns go left)  d5 columns left  d6 count  d7 temp
|  a0 source  a1 scratch  a2 written bits  a3 z mask  a4 charset mask
|  a5 run table  a6 height * 40

| next column: rewind the pointers and step one pixel
.macro	NEXTCOL mode
	subq.l	#1,%d5
	beq	cost_done
	move.w	%d4,%d1
	suba.l	%a6,%a2
.if \mode >= 1
	suba.l	%a6,%a3
.endif
.if \mode >= 2
	suba.l	%a6,%a4
.endif
	move.l	%a6,%d7
	lsl.l	#3,%d7
	suba.l	%d7,%a1
	tst.l	%d4
	bmi	5f
	addq.l	#1,%a1
	ror.b	#1,%d3
	bcc	6f
	addq.l	#1,%a2
.if \mode >= 1
	addq.l	#1,%a3
.endif
.if \mode >= 2
	addq.l	#1,%a4
.endif
	bra	6f
5:	subq.l	#1,%a1
	rol.b	#1,%d3
	bcc	6f
	subq.l	#1,%a2
.if \mode >= 1
	subq.l	#1,%a3
.endif
.if \mode >= 2
	subq.l	#1,%a4
.endif
6:
.endm

.macro	DECODE mode
	tst.w	%d0
	beq	.Lrun\@
	tst.l	%d6
	bne	.Ltr\@
	bra	.Lop\@

.Lrun\@:
	moveq	#0,%d7
	move.b	(%a0)+,%d7
	move.b	(%a5,%d7.w),%d2
	moveq	#0,%d0
	move.b	256(%a5,%d7.w),%d0
	bclr	#7,%d0
	bne	.Ltrx\@
	tst.b	%d0
	bne	.Lop\@
	move.b	(%a0)+,%d0
	beq	.Lrun\@

.Lop\@:
	move.w	%d0,%d6
	cmp.w	%d1,%d6
	bls	1f
	move.w	%d1,%d6
1:	sub.w	%d6,%d0
	sub.w	%d6,%d1
	subq.w	#1,%d6
2:
.if \mode == 0
	move.b	%d2,(%a1)
	or.b	%d3,(%a2)
.else
	move.b	(%a3),%d7
.if \mode == 2
	or.b	(%a4),%d7
	lea	40(%a4),%a4
.endif
	and.b	%d3,%d7
	bne	4f
	move.b	%d2,(%a1)
	or.b	%d3,(%a2)
4:	lea	40(%a3),%a3
.endif
	lea	320(%a1),%a1
	lea	40(%a2),%a2
	dbra	%d6,2b
	tst.w	%d1
	bne	3f
	NEXTCOL	\mode
3:	tst.w	%d0
	bne	.Lop\@
	bra	.Lrun\@

.Ltrx\@:
	tst.b	%d0
	bne	.Ltr\@
	move.b	(%a0)+,%d0
	beq	.Lrun\@

.Ltr\@:
	move.w	%d0,%d6
	cmp.w	%d1,%d6
	bls	1f
	move.w	%d1,%d6
1:	sub.w	%d6,%d0
	sub.w	%d6,%d1
	moveq	#0,%d7
	move.w	%d6,%d7
	lsl.l	#2,%d7
	add.w	%d6,%d7
	lsl.l	#3,%d7
	adda.l	%d7,%a2
.if \mode >= 1
	adda.l	%d7,%a3
.endif
.if \mode >= 2
	adda.l	%d7,%a4
.endif
	lsl.l	#3,%d7
	adda.l	%d7,%a1
	tst.w	%d1
	bne	3f
	NEXTCOL	\mode
3:	tst.w	%d0
	bne	.Ltr\@
	bra	.Lrun\@
.endm

| void cost_runs(const struct cost_runs *a)
	.globl	cost_runs
cost_runs:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a6
	move.l	R_SRC(%a6),%a0
	move.l	R_SP(%a6),%a1
	move.l	R_WP(%a6),%a2
	move.l	R_ZM(%a6),%a3
	move.l	R_CM(%a6),%a4
	move.l	R_TAB(%a6),%a5
	move.l	R_LEN(%a6),%d0
	move.l	R_PC(%a6),%d2
	move.l	R_MASKBIT(%a6),%d3
	move.l	R_HEIGHT(%a6),%d4
	move.l	R_COLUMNS(%a6),%d5
	move.l	R_TRANSP(%a6),%d6
	move.l	R_MODE(%a6),%d7
	moveq	#0,%d1
	move.w	%d4,%d1
	mulu.w	#40,%d1
	move.l	%d1,%a6
	move.w	%d4,%d1
	ext.l	%d6
	cmp.l	#1,%d7
	beq	.Lmode1
	bhi	.Lmode2
	DECODE	0
.Lmode1:
	DECODE	1
.Lmode2:
	DECODE	2
cost_done:
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| struct cost_scaled: struct cost_runs, then
	.set	S_YCLIP, 52
	.set	S_ADV, 56
	.set	S_DIR, 60
	.set	S_WTAB, 64

| registers:
|  d0 run length left  d1 source rows left in the column  d2 colour (bit
|  15: transparent)  d3 mask bit  d4 rows this step  d5 end row offset
|  d6 temp  d7 row offset (y * 40)  a0 source  a1 scratch column  a2
|  written bits column  a3 z mask column  a4 charset mask column  a5 word
|  run table  a6 yclip entry of the current source row
| locals: 0(sp) columns left, 4(sp) adv, 8(sp) height, 12(sp) dir,
| 16(sp) yclip
.macro	SCALED mode
.Lsmain\@:
	tst.w	%d0
	beq	.Lsfetch\@
.Lschunk\@:
	move.l	%d1,%d4
	cmp.l	%d0,%d4
	bls	1f
	move.l	%d0,%d4
1:	sub.l	%d4,%d0
	tst.w	%d2
	bmi	.Lsnext\@
	move.w	(%a6),%d7
	move.w	(%a6,%d4.l*2),%d5
	cmp.w	%d5,%d7
	bge	.Lsnext\@
2:
.if \mode == 0
	move.b	%d2,(%a1,%d7.w*8)
	or.b	%d3,(%a2,%d7.w)
.else
	move.b	(%a3,%d7.w),%d6
.if \mode == 2
	or.b	(%a4,%d7.w),%d6
.endif
	and.b	%d3,%d6
	bne	3f
	move.b	%d2,(%a1,%d7.w*8)
	or.b	%d3,(%a2,%d7.w)
3:
.endif
	add.w	#40,%d7
	cmp.w	%d5,%d7
	blt	2b
.Lsnext\@:
	lea	(%a6,%d4.l*2),%a6
	sub.l	%d4,%d1
	bne	.Lsfetch\@
	move.l	8(%sp),%d1
	subq.l	#1,(%sp)
	beq	cost_scaled_done
	move.l	4(%sp),%a6
	addq.l	#1,4(%sp)
	tst.b	(%a6)
	move.l	16(%sp),%a6
	beq	.Lsmain\@
	tst.l	12(%sp)
	bmi	5f
	addq.l	#1,%a1
	ror.b	#1,%d3
	bcc	.Lsmain\@
	addq.l	#1,%a2
	addq.l	#1,%a3
	addq.l	#1,%a4
	bra	.Lsmain\@
5:	subq.l	#1,%a1
	rol.b	#1,%d3
	bcc	.Lsmain\@
	subq.l	#1,%a2
	subq.l	#1,%a3
	subq.l	#1,%a4
	bra	.Lsmain\@
.Lsfetch\@:
	move.b	(%a0)+,%d6
	move.w	(%a5,%d6.w*2),%d2
	move.w	%d2,%d0
	lsr.w	#8,%d0
	and.w	#0x7f,%d0
	bne	.Lschunk\@
	move.b	(%a0)+,%d0
	beq	.Lsfetch\@
	bra	.Lschunk\@
.endm

| void cost_runs_scaled(const struct cost_scaled *a)
	.globl	cost_runs_scaled
cost_runs_scaled:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	lea	-20(%sp),%sp
	move.l	68(%sp),%a6
	move.l	R_COLUMNS(%a6),(%sp)
	move.l	S_ADV(%a6),4(%sp)
	move.l	R_HEIGHT(%a6),8(%sp)
	move.l	S_DIR(%a6),12(%sp)
	move.l	S_YCLIP(%a6),16(%sp)
	move.l	R_SRC(%a6),%a0
	move.l	R_SP(%a6),%a1
	move.l	R_WP(%a6),%a2
	move.l	R_ZM(%a6),%a3
	move.l	R_CM(%a6),%a4
	move.l	S_WTAB(%a6),%a5
	move.l	R_LEN(%a6),%d0
	moveq	#0,%d2
	move.b	R_PC+3(%a6),%d2
	tst.l	R_TRANSP(%a6)
	beq	1f
	bset	#15,%d2
1:	move.l	R_MASKBIT(%a6),%d3
	move.l	R_MODE(%a6),%d7
	move.l	R_HEIGHT(%a6),%d1
	move.l	S_YCLIP(%a6),%a6
	moveq	#0,%d6			| only its low byte changes from here
	cmp.l	#1,%d7
	beq	.Lsmode1
	bhi	.Lsmode2
	SCALED	0
.Lsmode1:
	SCALED	1
.Lsmode2:
	SCALED	2
cost_scaled_done:
	lea	20(%sp),%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| struct pix_draw offsets (costume.c)
	.set	P_PIX, 0
	.set	P_ROWS, 4
	.set	P_NROWS, 8
	.set	P_GROUPS, 12
	.set	P_NGROUPS, 16
	.set	P_ZM, 20
	.set	P_CM, 24
	.set	P_MODE, 28
	.set	P_LUT, 32
	.set	P_SP, 36
	.set	P_WP, 40

| registers per group (one output column):
|  d0 mask temp  d1 columns left  d2 source index  d3 pixel  d4 mask bit
|  d5 row (source offset high, yclip offset low)  d6 columns before
|  d7 rows left  a0 rows  a1 scratch column  a2 written bits column
|  a3 z mask column  a4 charset mask column  a5 lut  a6 source column
| locals: 0(sp) groups left, 4(sp) next group, 8(sp) args
.macro	PIXCOLS mode
.Lpg\@:
	move.l	8(%sp),%a0
	move.l	4(%sp),%a6
	move.w	(%a6)+,%d0
	moveq	#0,%d4
	move.b	(%a6)+,%d4
	moveq	#0,%d1
	move.b	(%a6)+,%d1
	move.w	(%a6)+,%d2
	move.w	(%a6)+,%d6
	move.l	%a6,4(%sp)
	move.l	P_SP(%a0),%a1
	add.w	%d0,%a1
	move.l	P_WP(%a0),%a2
	add.l	%d1,%a2
.if \mode
	move.l	P_ZM(%a0),%a3
	add.l	%d1,%a3
.if \mode == 2
	move.l	P_CM(%a0),%a4
	add.l	%d1,%a4
.endif
.endif
	move.l	P_PIX(%a0),%a6
	add.w	%d2,%a6
	move.l	P_NROWS(%a0),%d7
	subq.w	#1,%d7
	move.l	P_ROWS(%a0),%a0
.Lpr\@:
	move.l	(%a0)+,%d5
.if \mode
	move.b	(%a3,%d5.w),%d0
.if \mode == 2
	or.b	(%a4,%d5.w),%d0
.endif
	and.b	%d4,%d0
	bne	.Lpn\@
.endif
	swap	%d5
	move.b	(%a6,%d5.w),%d3
	move.b	(%a5,%d3.w),%d3
	cmp.b	#13,%d3
	beq	.Lpt\@
.Lph\@:
	swap	%d5
	move.b	%d3,(%a1,%d5.w*8)
	or.b	%d4,(%a2,%d5.w)
.Lpn\@:
	dbra	%d7,.Lpr\@
	subq.l	#1,(%sp)
	bne	.Lpg\@
	bra	cel_pix_done
.Lpt\@:
	move.w	%d6,%d1
	beq	.Lpn\@
	move.w	%d5,%d2
	subq.w	#1,%d1
1:	subq.w	#1,%d2
	move.b	(%a6,%d2.w),%d3
	move.b	(%a5,%d3.w),%d3
	cmp.b	#13,%d3
	bne	.Lph\@
	dbra	%d1,1b
	bra	.Lpn\@
.endm

| void cel_pix_draw(const struct pix_draw *a)
| C model: cel_pix_draw in costume.c. Each output pixel takes the last
| opaque source pixel of its group of columns, on the source row drawn.
	.globl	cel_pix_draw
cel_pix_draw:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a0
	lea	-12(%sp),%sp
	move.l	%a0,8(%sp)
	move.l	P_NGROUPS(%a0),(%sp)
	beq	cel_pix_done
	tst.l	P_NROWS(%a0)
	beq	cel_pix_done
	move.l	P_GROUPS(%a0),4(%sp)
	move.l	P_LUT(%a0),%a5
	moveq	#0,%d3
	move.l	P_MODE(%a0),%d0
	beq	.Lpm0
	subq.l	#1,%d0
	beq	.Lpm1
	PIXCOLS	2
.Lpm0:
	PIXCOLS	0
.Lpm1:
	PIXCOLS	1
cel_pix_done:
	lea	12(%sp),%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

| One 8-pixel group: chunky longs d2 (pixels 0-3), d3 (4-7) to planes,
| 8x8 bit transpose as c2p8 in gfx.c. Leaves planes 0-3 in d3 and 4-7 in
| d2, low byte first.
.macro	C2P
	move.l	%d2,%d4
	lsr.l	#7,%d4
	eor.l	%d2,%d4
	andi.l	#0x00AA00AA,%d4
	eor.l	%d4,%d2
	lsl.l	#7,%d4
	eor.l	%d4,%d2
	move.l	%d3,%d4
	lsr.l	#7,%d4
	eor.l	%d3,%d4
	andi.l	#0x00AA00AA,%d4
	eor.l	%d4,%d3
	lsl.l	#7,%d4
	eor.l	%d4,%d3
	move.l	%d2,%d4
	lsr.l	%d6,%d4
	eor.l	%d2,%d4
	andi.l	#0x0000CCCC,%d4
	eor.l	%d4,%d2
	lsl.l	%d6,%d4
	eor.l	%d4,%d2
	move.l	%d3,%d4
	lsr.l	%d6,%d4
	eor.l	%d3,%d4
	andi.l	#0x0000CCCC,%d4
	eor.l	%d4,%d3
	lsl.l	%d6,%d4
	eor.l	%d4,%d3
	move.l	%d3,%d4
	lsr.l	#4,%d4
	eor.l	%d2,%d4
	andi.l	#0x0F0F0F0F,%d4
	eor.l	%d4,%d2
	lsl.l	#4,%d4
	eor.l	%d4,%d3
.endm

| void cost_flush_row(uint8_t *wr, const uint8_t *c, uint8_t *d, int n)
| Writes the n 8-pixel groups of chunky row c whose written-bit byte is
| non-zero into the planes at d (40 bytes apart), clearing those bytes.
	.globl	cost_flush_row
cost_flush_row:
	movem.l	%d2-%d7/%a2-%a3,-(%sp)
	move.l	36(%sp),%a0
	move.l	40(%sp),%a1
	move.l	44(%sp),%a2
	move.l	48(%sp),%d0
	subq.l	#1,%d0
	bmi	.Lfdone
	moveq	#14,%d6
.Lfloop:
	move.b	(%a0),%d1
	beq	.Lfnext
	clr.b	(%a0)
	move.l	(%a1),%d2
	move.l	4(%a1),%d3
	C2P
	movea.l	%a2,%a3
	cmp.b	#-1,%d1
	beq	.Lffull
	moveq	#7,%d7
1:	move.b	(%a3),%d4
	eor.b	%d4,%d3
	and.b	%d1,%d3
	eor.b	%d3,%d4
	move.b	%d4,(%a3)
	lea	40(%a3),%a3
	lsr.l	#8,%d3
	cmp.w	#4,%d7
	bne	2f
	move.l	%d2,%d3
2:	dbra	%d7,1b
	bra	.Lfnext
.Lffull:
	move.b	%d3,(%a3)
	lsr.l	#8,%d3
	move.b	%d3,40(%a3)
	lsr.l	#8,%d3
	move.b	%d3,80(%a3)
	lsr.l	#8,%d3
	move.b	%d3,120(%a3)
	move.b	%d2,160(%a3)
	lsr.l	#8,%d2
	move.b	%d2,200(%a3)
	lsr.l	#8,%d2
	move.b	%d2,240(%a3)
	lsr.l	#8,%d2
	move.b	%d2,280(%a3)
.Lfnext:
	addq.l	#1,%a0
	addq.l	#8,%a1
	addq.l	#1,%a2
	dbra	%d0,.Lfloop
.Lfdone:
	movem.l	(%sp)+,%d2-%d7/%a2-%a3
	rts
