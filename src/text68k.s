| scumm5-amiga - charset glyph rows into the planar screen, 68020
| SPDX-License-Identifier: GPL-3.0-or-later

| struct glyph_job (text.c)
	.set	J_SRC, 0
	.set	J_DST, 4
	.set	J_MASK, 8
	.set	J_TAB, 12
	.set	J_CPACK, 16
	.set	J_ROWS, 20
	.set	J_GROUPS, 22
	.set	J_LAST, 24
	.set	J_LMASK, 26
	.set	J_SHIFT, 28
	.set	J_BPP, 30
	.set	J_RSH, 32
	.set	J_NB, 34

| Takes the next 8 source bits in the low byte of d7: d0 bit buffer
| (left-aligned), d1 bits in it.
.macro	TOPBYTE
	cmp.w	#8,%d1
	bge.s	1f
	moveq	#0,%d7
	move.b	(%a0)+,%d7
	moveq	#24,%d6
	sub.w	%d1,%d6
	lsl.l	%d6,%d7
	or.l	%d7,%d0
	addq.w	#8,%d1
1:	move.l	%d0,%d7
	rol.l	#8,%d7
.endm

| 4 pixels of 2 bits in d7.b to the colour masks d2-d4, a nibble each
.macro	SPREAD2
	and.w	#0xFF,%d7
	move.w	(%a3,%d7.w*2),%d7
	lsl.l	#4,%d4
	moveq	#15,%d6
	and.w	%d7,%d6
	or.l	%d6,%d4
	lsr.w	#4,%d7
	lsl.l	#4,%d3
	moveq	#15,%d6
	and.w	%d7,%d6
	or.l	%d6,%d3
	lsr.w	#4,%d7
	lsl.l	#4,%d2
	or.w	%d7,%d2
.endm

| Plane bits d6 for the next plane from d2-d4 by the colour bits in d5
.macro	SELECT
	moveq	#0,%d6
	lsr.l	#1,%d5
	bcc.s	1f
	or.l	%d2,%d6
1:	lsr.l	#1,%d5
	bcc.s	2f
	or.l	%d3,%d6
2:	lsr.l	#1,%d5
	bcc.s	3f
	or.l	%d4,%d6
3:
.endm

| All 8 planes of one row, n bytes wide
.macro	PLANES n
	moveq	#7,%d1
9:	SELECT
.if \n == 1
	move.b	(%a4),%d7
	or.b	%d0,%d7
	eor.b	%d0,%d7
	or.b	%d6,%d7
	move.b	%d7,(%a4)
.endif
.if \n == 2
	move.w	(%a4),%d7
	or.w	%d0,%d7
	eor.w	%d0,%d7
	or.w	%d6,%d7
	move.w	%d7,(%a4)
.endif
.if \n == 3
	move.w	1(%a4),%d7
	or.w	%d0,%d7
	eor.w	%d0,%d7
	or.w	%d6,%d7
	move.w	%d7,1(%a4)
	swap	%d0
	swap	%d6
	move.b	(%a4),%d7
	or.b	%d0,%d7
	eor.b	%d0,%d7
	or.b	%d6,%d7
	move.b	%d7,(%a4)
	swap	%d0
.endif
.if \n == 4
	move.l	(%a4),%d7
	or.l	%d0,%d7
	eor.l	%d0,%d7
	or.l	%d6,%d7
	move.l	%d7,(%a4)
.endif
	lea	40(%a4),%a4
	dbra	%d1,9b
.endm

| uint32_t glyph_rows(struct glyph_job *j)
| Returns the OR of all pixel masks drawn (0: nothing drawn).
	.globl	glyph_rows
glyph_rows:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	48(%sp),%a6
	move.l	J_SRC(%a6),%a0
	move.l	J_DST(%a6),%a1
	move.l	J_MASK(%a6),%a2
	move.l	J_TAB(%a6),%a3
	sub.l	%a5,%a5
	moveq	#0,%d0
	moveq	#0,%d1
.Lrow:
	moveq	#0,%d2
	moveq	#0,%d3
	moveq	#0,%d4
	move.w	J_GROUPS(%a6),%d5
	cmp.w	#1,J_BPP(%a6)
	beq	.Lb1
	bra.s	.Lg2t
.Lg2:
	TOPBYTE
	lsl.l	#8,%d0
	subq.w	#8,%d1
	SPREAD2
.Lg2t:
	dbra	%d5,.Lg2
	move.w	J_LAST(%a6),%d5
	beq	.Lalign
	TOPBYTE
	lsl.l	%d5,%d0
	sub.w	%d5,%d1
	and.w	J_LMASK(%a6),%d7
	SPREAD2
	bra	.Lalign
.Lb1:
	bra.s	.Lg1t
.Lg1:
	TOPBYTE
	lsl.l	#8,%d0
	subq.w	#8,%d1
	lsl.l	#8,%d2
	move.b	%d7,%d2
.Lg1t:
	dbra	%d5,.Lg1
	move.w	J_LAST(%a6),%d5
	beq	.Lalign
	TOPBYTE
	lsl.l	%d5,%d0
	sub.w	%d5,%d1
	and.w	J_LMASK(%a6),%d7
	lsl.l	#8,%d2
	move.b	%d7,%d2
.Lalign:
	move.w	J_SHIFT(%a6),%d5
	lsl.l	%d5,%d2
	lsl.l	%d5,%d3
	lsl.l	%d5,%d4
	move.l	%d0,-(%sp)
	move.l	%d1,-(%sp)
	move.l	%d2,%d0
	or.l	%d3,%d0
	or.l	%d4,%d0
	beq	.Lnext
	move.l	%a5,%d7
	or.l	%d0,%d7
	move.l	%d7,%a5
	move.w	J_RSH(%a6),%d5
	lsr.l	%d5,%d0
	lsr.l	%d5,%d2
	lsr.l	%d5,%d3
	lsr.l	%d5,%d4
	move.l	J_CPACK(%a6),%d5
	move.l	%a1,%a4
	move.w	J_NB(%a6),%d7
	cmp.w	#2,%d7
	blt	.Lp1
	beq	.Lp2
	cmp.w	#3,%d7
	beq	.Lp3
	PLANES	4
	bra	.Lmask
.Lp1:	PLANES	1
	bra	.Lmask
.Lp2:	PLANES	2
	bra	.Lmask
.Lp3:	PLANES	3
.Lmask:
	move.l	%a2,%d7
	beq.s	.Lnext
	move.w	J_NB(%a6),%d7
	cmp.w	#2,%d7
	blt.s	.Lm1
	beq.s	.Lm2
	cmp.w	#3,%d7
	beq.s	.Lm3
	or.l	%d0,(%a2)
	bra.s	.Lnext
.Lm1:	or.b	%d0,(%a2)
	bra.s	.Lnext
.Lm2:	or.w	%d0,(%a2)
	bra.s	.Lnext
.Lm3:	or.w	%d0,1(%a2)
	swap	%d0
	or.b	%d0,(%a2)
.Lnext:
	move.l	(%sp)+,%d1
	move.l	(%sp)+,%d0
	lea	320(%a1),%a1
	move.l	%a2,%d7
	beq.s	1f
	lea	40(%a2),%a2
1:	subq.w	#1,J_ROWS(%a6)
	bne	.Lrow
	move.l	%a5,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts
