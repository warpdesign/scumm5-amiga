/* scumm5-amiga - minimal freestanding AmigaOS interface
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Library calls are inlined register-argument jsr's through the base in a6.
 * Libraries preserve everything but d0/d1/a0/a1; scratch registers used as
 * inputs are therefore declared in/out, the others clobbered.
 */
#ifndef AMIGA_H
#define AMIGA_H

typedef unsigned char UBYTE;
typedef unsigned short UWORD;
typedef unsigned long ULONG;
typedef signed short WORD;
typedef signed long LONG;
typedef void *APTR;

extern APTR SysBase;
extern APTR DOSBase;
extern APTR GfxBase;

#define MEMF_CHIP  (1UL << 1)
#define MEMF_LARGEST (1UL << 17)
#define MEMF_CLEAR (1UL << 16)
#define MODE_OLDFILE 1005
#define OFFSET_BEGINNING (-1)

/* struct GfxBase offsets */
#define GFX_ACTIVIEW 34
#define GFX_COPINIT  38
#define GFX_VBLANK   160	/* BYTE: vertical blank rate, 50 or 60 */

#define NT_INTERRUPT 2
#define INTB_VERTB 5

struct Node {
	struct Node *ln_Succ, *ln_Pred;
	UBYTE ln_Type;
	signed char ln_Pri;
	char *ln_Name;
};

struct Interrupt {
	struct Node is_Node;
	APTR is_Data;
	void (*is_Code)(void);
};

#define CUSTOM ((volatile UWORD *)0xdff000)
#define REG(off) (CUSTOM[(off) / 2])
#define CIAA_PRA (*(volatile UBYTE *)0xbfe001)

static inline ULONG AvailMem(ULONG req)
{
	register ULONG d1 __asm("d1") = req;
	register ULONG d0 __asm("d0");
	register APTR a6 __asm("a6") = SysBase;
	__asm volatile("jsr -216(%%a6)" : "=r"(d0), "+r"(d1) : "r"(a6) : "a0", "a1", "cc", "memory");
	return d0;
}

static inline APTR OpenLibrary(const char *name, ULONG ver)
{
	register APTR d0 __asm("d0") = (APTR)ver;
	register const char *a1 __asm("a1") = name;
	register APTR a6 __asm("a6") = SysBase;
	__asm volatile("jsr -552(%%a6)" : "+r"(d0), "+r"(a1) : "r"(a6) : "d1", "a0", "cc", "memory");
	return d0;
}

static inline void CloseLibrary(APTR lib)
{
	register APTR a1 __asm("a1") = lib;
	register APTR a6 __asm("a6") = SysBase;
	__asm volatile("jsr -414(%%a6)" : "+r"(a1) : "r"(a6) : "d0", "d1", "a0", "cc", "memory");
}

static inline APTR AllocMem(ULONG size, ULONG req)
{
	register ULONG d0 __asm("d0") = size;
	register ULONG d1 __asm("d1") = req;
	register APTR a6 __asm("a6") = SysBase;
	__asm volatile("jsr -198(%%a6)" : "+r"(d0), "+r"(d1) : "r"(a6) : "a0", "a1", "cc", "memory");
	return (APTR)d0;
}

static inline void FreeMem(APTR mem, ULONG size)
{
	register APTR a1 __asm("a1") = mem;
	register ULONG d0 __asm("d0") = size;
	register APTR a6 __asm("a6") = SysBase;
	__asm volatile("jsr -210(%%a6)" : "+r"(a1), "+r"(d0) : "r"(a6) : "d1", "a0", "cc", "memory");
}

static inline LONG Open(const char *name, LONG mode)
{
	register LONG d1 __asm("d1") = (LONG)name;
	register LONG d2 __asm("d2") = mode;
	register LONG d0 __asm("d0");
	register APTR a6 __asm("a6") = DOSBase;
	__asm volatile("jsr -30(%%a6)" : "=r"(d0), "+r"(d1) : "r"(d2), "r"(a6) : "a0", "a1", "cc", "memory");
	return d0;
}

static inline void Close(LONG fh)
{
	register LONG d1 __asm("d1") = fh;
	register APTR a6 __asm("a6") = DOSBase;
	__asm volatile("jsr -36(%%a6)" : "+r"(d1) : "r"(a6) : "d0", "a0", "a1", "cc", "memory");
}

static inline LONG Read(LONG fh, APTR buf, LONG len)
{
	register LONG d1 __asm("d1") = fh;
	register APTR d2 __asm("d2") = buf;
	register LONG d3 __asm("d3") = len;
	register LONG d0 __asm("d0");
	register APTR a6 __asm("a6") = DOSBase;
	__asm volatile("jsr -42(%%a6)" : "=r"(d0), "+r"(d1) : "r"(d2), "r"(d3), "r"(a6) : "a0", "a1", "cc", "memory");
	return d0;
}

static inline LONG Seek(LONG fh, LONG pos, LONG mode)
{
	register LONG d1 __asm("d1") = fh;
	register LONG d2 __asm("d2") = pos;
	register LONG d3 __asm("d3") = mode;
	register LONG d0 __asm("d0");
	register APTR a6 __asm("a6") = DOSBase;
	__asm volatile("jsr -66(%%a6)" : "=r"(d0), "+r"(d1) : "r"(d2), "r"(d3), "r"(a6) : "a0", "a1", "cc", "memory");
	return d0;
}

/* ds[0] days, ds[1] minutes, ds[2] ticks (1/50 s) */
static inline void DateStamp(LONG *ds)
{
	register LONG *d1 __asm("d1") = ds;
	register APTR a6 __asm("a6") = DOSBase;
	__asm volatile("jsr -192(%%a6)" : "+r"(d1) : "r"(a6) : "d0", "a0", "a1", "cc", "memory");
}

static inline void AddIntServer(ULONG num, struct Interrupt *i)
{
	register ULONG d0 __asm("d0") = num;
	register struct Interrupt *a1 __asm("a1") = i;
	register APTR a6 __asm("a6") = SysBase;
	__asm volatile("jsr -168(%%a6)" : "+r"(d0), "+r"(a1) : "r"(a6) : "d1", "a0", "cc", "memory");
}

static inline void RemIntServer(ULONG num, struct Interrupt *i)
{
	register ULONG d0 __asm("d0") = num;
	register struct Interrupt *a1 __asm("a1") = i;
	register APTR a6 __asm("a6") = SysBase;
	__asm volatile("jsr -174(%%a6)" : "+r"(d0), "+r"(a1) : "r"(a6) : "d1", "a0", "cc", "memory");
}

static inline void PutStr(const char *s)
{
	register const char *d1 __asm("d1") = s;
	register APTR a6 __asm("a6") = DOSBase;
	__asm volatile("jsr -948(%%a6)" : "+r"(d1) : "r"(a6) : "d0", "a0", "a1", "cc", "memory");
}

static inline void LoadView(APTR view)
{
	register APTR a1 __asm("a1") = view;
	register APTR a6 __asm("a6") = GfxBase;
	__asm volatile("jsr -222(%%a6)" : "+r"(a1) : "r"(a6) : "d0", "d1", "a0", "cc", "memory");
}

static inline void OwnBlitter(void)
{
	register APTR a6 __asm("a6") = GfxBase;
	__asm volatile("jsr -456(%%a6)" : : "r"(a6) : "d0", "d1", "a0", "a1", "cc", "memory");
}

static inline void DisownBlitter(void)
{
	register APTR a6 __asm("a6") = GfxBase;
	__asm volatile("jsr -462(%%a6)" : : "r"(a6) : "d0", "d1", "a0", "a1", "cc", "memory");
}

static inline void WaitTOF(void)
{
	register APTR a6 __asm("a6") = GfxBase;
	__asm volatile("jsr -270(%%a6)" : : "r"(a6) : "d0", "d1", "a0", "a1", "cc", "memory");
}

#endif
