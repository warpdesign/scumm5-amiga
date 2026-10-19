# scumm5-amiga
# SPDX-License-Identifier: GPL-3.0-or-later

AROS_BUILD ?= $(HOME)/Documents/Dev/AROS-build
TOOLCHAIN  ?= $(AROS_BUILD)/toolchain
ELF2HUNK   ?= $(AROS_BUILD)/m68k/bin/darwin-aarch64/tools/elf2hunk
CC          = $(TOOLCHAIN)/m68k-aros-gcc
HOSTCC     ?= cc
PYTHON     ?= python3

DATA ?= scumm-data/mi2demo
GAME ?= MI2DEMO
PACK_SRC = $(DATA)/$(GAME).000 $(wildcard $(DATA)/$(GAME).00[1-9])

# -mcpu=68020 (not -m68020) is what selects the m68020 libgcc multilib.
# Loop distribution could turn our own memcpy/memset loops into calls to themselves.
CFLAGS  = -mcpu=68020 -Os -ffreestanding -fno-builtin -fomit-frame-pointer \
          -fno-tree-loop-distribute-patterns -Wall -Wextra -std=gnu99 -fno-common -Isrc
LDFLAGS = -nostdlib -nostartfiles -r
HOSTCFLAGS = -std=gnu99 -g -O1 -Wall -Wextra -fsanitize=address,undefined \
             -fno-omit-frame-pointer -Isrc

OUT = build/amiga
HOST = build/host
HEADERS = $(wildcard src/*.h src/scumm/*.h)

COMMON_SRC = util res heap
SCUMM_SRC = vm ops5 object room boxes actor costume camera verbs text sound input engine trace
AMIGA_PROGS = roomview restest scummrun
HOST_PROGS = restest scummrun

AMIGA_COMMON = $(OUT)/start.o $(OUT)/plat_amiga.o $(COMMON_SRC:%=$(OUT)/%.o)
HOST_COMMON = $(HOST)/plat_host.o $(COMMON_SRC:%=$(HOST)/%.o)

PACK = $(OUT)/$(shell echo $(GAME) | tr A-Z a-z).s5p

all: $(AMIGA_PROGS:%=$(OUT)/%) $(HOST_PROGS:%=$(HOST)/%) $(PACK)

AMIGA_SCUMM = $(SCUMM_SRC:%=$(OUT)/scumm/%.o)
HOST_SCUMM = $(SCUMM_SRC:%=$(HOST)/scumm/%.o)

$(OUT) $(HOST) $(OUT)/scumm $(HOST)/scumm:
	mkdir -p $@

$(OUT)/scumm/%.o: src/scumm/%.c $(HEADERS) | $(OUT)/scumm
	$(CC) $(CFLAGS) -c $< -o $@

$(HOST)/scumm/%.o: src/scumm/%.c $(HEADERS) | $(HOST)/scumm
	$(HOSTCC) $(HOSTCFLAGS) -c $< -o $@

$(OUT)/scummrun.elf: $(OUT)/scummrun.o $(AMIGA_COMMON) $(AMIGA_SCUMM)
	$(CC) $(LDFLAGS) $(AMIGA_COMMON) $(AMIGA_SCUMM) $< -lgcc -o $@

$(HOST)/scummrun: $(HOST)/scummrun.o $(HOST_COMMON) $(HOST_SCUMM)
	$(HOSTCC) $(HOSTCFLAGS) $(HOST_COMMON) $(HOST_SCUMM) $< -o $@

$(OUT)/%.o: src/%.c $(HEADERS) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: src/%.s | $(OUT)
	$(CC) -mcpu=68020 -c $< -o $@

$(OUT)/%.elf: $(OUT)/%.o $(AMIGA_COMMON)
	$(CC) $(LDFLAGS) $(AMIGA_COMMON) $< -lgcc -o $@

$(OUT)/%: $(OUT)/%.elf
	$(ELF2HUNK) $< $@

$(HOST)/%.o: src/%.c $(HEADERS) | $(HOST)
	$(HOSTCC) $(HOSTCFLAGS) -c $< -o $@

$(HOST)/%: $(HOST)/%.o $(HOST_COMMON)
	$(HOSTCC) $(HOSTCFLAGS) $(HOST_COMMON) $< -o $@

$(PACK): tools/mkpack.py tools/room2planar.py $(PACK_SRC) | $(OUT)
	$(PYTHON) -I tools/mkpack.py $(PACK_SRC) -o $@

clean:
	rm -rf build

.PHONY: all clean
.DELETE_ON_ERROR:
.SECONDARY:
