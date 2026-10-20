| scumm5-amiga - process entry point
| SPDX-License-Identifier: GPL-3.0-or-later
|
| AmigaDOS starts the first hunk with a0 = argument string, d0 = its length.
| plat_exit(rc) unwinds to here from anywhere (fatal errors only: only the
| Workbench message and dos.library are seen to).

	.text
	.globl	_start
_start:
	movem.l	%d2-%d7/%a2-%a6,-(%sp)
	move.l	%sp,saved_sp
	move.l	4,%a6
	move.l	%a6,SysBase
	move.l	%d0,-(%sp)
	move.l	%a0,-(%sp)
	jsr	amiga_main
	addq.l	#8,%sp
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

	.globl	plat_exit
plat_exit:
	move.l	4(%sp),%d2
	move.l	saved_sp,%sp
	jsr	amiga_cleanup
	move.l	%d2,%d0
	movem.l	(%sp)+,%d2-%d7/%a2-%a6
	rts

	.bss
	.globl	SysBase
	.align	2
SysBase:
	.skip	4
saved_sp:
	.skip	4
