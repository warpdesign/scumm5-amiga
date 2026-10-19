| scumm5-amiga - vertical blank interrupt server: counts frames
| SPDX-License-Identifier: GPL-3.0-or-later
|
| Called by exec with a1 = is_Data (pointer to the counter). Returning with
| the Z flag set lets the other VERTB servers run.

	.text
	.globl	vbl_server
vbl_server:
	addq.l	#1,(%a1)
	moveq	#0,%d0
	rts
