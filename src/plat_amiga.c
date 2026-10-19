/* scumm5-amiga - platform layer for AmigaOS (Kickstart 3.x, AROS)
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "amiga.h"
#include "platform.h"

#define MODE_NEWFILE 1006
#define OFFSET_END 1
#define MAX_ARGS 16

APTR DOSBase;

int app_main(int argc, char **argv);

/* AmigaDOS passes one argument string; split it like a C runtime would. */
int amiga_main(const char *args, ULONG arglen)
{
	static char buf[256];
	static char *argv[MAX_ARGS + 1];
	char *d = buf;
	int argc = 1, rc;

	argv[0] = "prog";
	while (arglen && argc < MAX_ARGS && d < buf + sizeof(buf) - 1) {
		while (arglen && (*args == ' ' || *args == '\t' || *args == '\n')) {
			args++;
			arglen--;
		}
		if (!arglen)
			break;
		argv[argc++] = d;
		if (*args == '"') {
			args++;
			arglen--;
			while (arglen && *args != '"' && d < buf + sizeof(buf) - 1) {
				*d++ = *args++;
				arglen--;
			}
			if (arglen) {
				args++;
				arglen--;
			}
		} else {
			while (arglen && *args > ' ' && d < buf + sizeof(buf) - 1) {
				*d++ = *args++;
				arglen--;
			}
		}
		*d++ = 0;
	}
	argv[argc] = 0;

	DOSBase = OpenLibrary("dos.library", 36);
	if (!DOSBase)
		return 20;
	rc = app_main(argc, argv);
	CloseLibrary(DOSBase);
	return rc;
}

static void uae_log(const char *s)
{
	typedef LONG (*uaelib_fn)(LONG, const char *);
	uaelib_fn uae = (uaelib_fn)0xf0ff60;
	UWORD op = *(volatile UWORD *)0xf0ff60;
	if (op == 0x4eb9 || op == 0xa00e)
		uae(86, s);
}

static inline LONG Write(LONG fh, const void *buf, LONG len)
{
	register LONG d1 __asm("d1") = fh;
	register const void *d2 __asm("d2") = buf;
	register LONG d3 __asm("d3") = len;
	register LONG d0 __asm("d0");
	register APTR a6 __asm("a6") = DOSBase;
	__asm volatile("jsr -48(%%a6)" : "=r"(d0), "+r"(d1) : "r"(d2), "r"(d3), "r"(a6) : "a0", "a1", "cc", "memory");
	return d0;
}

plat_file plat_open(const char *path)
{
	return (plat_file)Open(path, MODE_OLDFILE);
}

plat_file plat_create(const char *path)
{
	return (plat_file)Open(path, MODE_NEWFILE);
}

void plat_close(plat_file f)
{
	Close((LONG)f);
}

int32_t plat_read(plat_file f, void *buf, uint32_t n)
{
	return Read((LONG)f, buf, (LONG)n);
}

int32_t plat_write(plat_file f, const void *buf, uint32_t n)
{
	return Write((LONG)f, buf, (LONG)n);
}

int plat_seek(plat_file f, uint32_t pos)
{
	return Seek((LONG)f, (LONG)pos, OFFSET_BEGINNING) < 0 ? -1 : 0;
}

int32_t plat_size(plat_file f)
{
	LONG cur = Seek((LONG)f, 0, OFFSET_END);
	LONG end = Seek((LONG)f, cur, OFFSET_BEGINNING);
	return end;
}

void *plat_alloc(uint32_t n, int flags)
{
	ULONG req = 0;
	if (flags & PLAT_CHIP)
		req |= MEMF_CHIP;
	if (flags & PLAT_CLEAR)
		req |= MEMF_CLEAR;
	return AllocMem(n, req);
}

void plat_free(void *p, uint32_t n)
{
	if (p)
		FreeMem(p, n);
}

void plat_log(const char *s)
{
	PutStr(s);
	PutStr("\n");
	uae_log(s);
}

uint32_t plat_ticks(void)
{
	LONG ds[3];
	DateStamp(ds);
	return (uint32_t)ds[1] * 3000 + (uint32_t)ds[2];
}

void *memcpy(void *d, const void *s, size_t n)
{
	uint8_t *dp = d;
	const uint8_t *sp = s;

	if (n >= 8 && !(((uintptr_t)dp ^ (uintptr_t)sp) & 1)) {
		if ((uintptr_t)dp & 1) {
			*dp++ = *sp++;
			n--;
		}
		{
			uint32_t *dl = (uint32_t *)dp;
			const uint32_t *sl = (const uint32_t *)sp;
			size_t l = n >> 4;
			while (l--) {
				dl[0] = sl[0];
				dl[1] = sl[1];
				dl[2] = sl[2];
				dl[3] = sl[3];
				dl += 4;
				sl += 4;
			}
			l = (n >> 2) & 3;
			while (l--)
				*dl++ = *sl++;
			dp = (uint8_t *)dl;
			sp = (const uint8_t *)sl;
			n &= 3;
		}
	}
	while (n--)
		*dp++ = *sp++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	uint8_t *dp = d;
	const uint8_t *sp = s;
	if (dp <= sp || dp >= sp + n)
		return memcpy(d, s, n);
	dp += n;
	sp += n;
	while (n--)
		*--dp = *--sp;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	uint8_t *dp = d;

	if (n >= 8) {
		uint32_t v = (uint8_t)c;
		v |= v << 8;
		v |= v << 16;
		if ((uintptr_t)dp & 1) {
			*dp++ = (uint8_t)c;
			n--;
		}
		{
			uint32_t *dl = (uint32_t *)dp;
			size_t l = n >> 2;
			while (l--)
				*dl++ = v;
			dp = (uint8_t *)dl;
			n &= 3;
		}
	}
	while (n--)
		*dp++ = (uint8_t)c;
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *pa = a, *pb = b;
	for (; n; n--, pa++, pb++)
		if (*pa != *pb)
			return *pa - *pb;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return (size_t)(p - s);
}
