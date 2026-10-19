| scumm5-amiga - process entry point
| SPDX-License-Identifier: GPL-3.0-or-later
|
| AmigaDOS starts the first hunk with a0 = argument string, d0 = its length.

	.text
	.globl	_start
_start:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	4,%a6
	move.l	%a6,SysBase
	move.l	%d0,-(%sp)
	move.l	%a0,-(%sp)
	jsr	amiga_main
	addq.l	#8,%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

	.bss
	.globl	SysBase
	.align	2
SysBase:
	.skip	4
