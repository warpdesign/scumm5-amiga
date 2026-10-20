| scumm5-amiga - image strip decoding for the 68020
| SPDX-License-Identifier: GPL-3.0-or-later
|
| C model: unpack_columns in src/scumm/gfx.c (no mask, all rows kept).

	.text

| void unpack_columns_fast(uint8_t *dst, const uint8_t *src, int n)
| The 8 plane columns of one strip, n rows each, from PackBits-style runs
| (0..127: that many plus one literals, -127..-1: one byte repeated
| 1 - c times, -128: nothing) into the interleaved screen: rows are 320
| bytes apart, planes 40.
|  d0 code/count  d1 rows left in the column  d2 planes left  d3 fill byte
|  d4 n  a0 source  a1 destination  a2 column start
	.globl	unpack_columns_fast
unpack_columns_fast:
	movem.l	%d2-%d4/%a2,-(%sp)
	move.l	20(%sp),%a2
	move.l	24(%sp),%a0
	move.l	28(%sp),%d4
	move.l	%a2,%a1
	move.l	%d4,%d1
	moveq	#8,%d2
.Lucode:
	move.b	(%a0)+,%d0
	bmi	.Lufill
	ext.w	%d0
	addq.w	#1,%d0
.Lulit:
	cmp.w	%d1,%d0
	bhi	.Lulsplit
	sub.w	%d0,%d1
	subq.w	#1,%d0
1:	move.b	(%a0)+,(%a1)
	lea	320(%a1),%a1
	dbra	%d0,1b
	tst.w	%d1
	bne	.Lucode
	bsr	.Lunext
	bne	.Lucode
	bra	.Ludone
.Lulsplit:
	sub.w	%d1,%d0
	subq.w	#1,%d1
1:	move.b	(%a0)+,(%a1)
	lea	320(%a1),%a1
	dbra	%d1,1b
	bsr	.Lunext
	bne	.Lulit
	bra	.Ludone
.Lufill:
	cmp.b	#-128,%d0
	beq	.Lucode
	ext.w	%d0
	neg.w	%d0
	addq.w	#1,%d0
	move.b	(%a0)+,%d3
.Lurep:
	cmp.w	%d1,%d0
	bhi	.Lufsplit
	sub.w	%d0,%d1
	subq.w	#1,%d0
1:	move.b	%d3,(%a1)
	lea	320(%a1),%a1
	dbra	%d0,1b
	tst.w	%d1
	bne	.Lucode
	bsr	.Lunext
	bne	.Lucode
	bra	.Ludone
.Lufsplit:
	sub.w	%d1,%d0
	subq.w	#1,%d1
1:	move.b	%d3,(%a1)
	lea	320(%a1),%a1
	dbra	%d1,1b
	bsr	.Lunext
	bne	.Lurep
.Ludone:
	movem.l	(%sp)+,%d2-%d4/%a2
	rts

| next plane column: Z set when all 8 are done
.Lunext:
	lea	40(%a2),%a2
	move.l	%a2,%a1
	move.w	%d4,%d1
	subq.w	#1,%d2
	rts
