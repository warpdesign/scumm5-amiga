# scumm5-amiga
# SPDX-License-Identifier: GPL-3.0-or-later

AROS_BUILD ?= $(HOME)/Documents/Dev/AROS-build
TOOLCHAIN  ?= $(AROS_BUILD)/toolchain
ELF2HUNK   ?= $(AROS_BUILD)/m68k/bin/darwin-aarch64/tools/elf2hunk
CC          = $(TOOLCHAIN)/m68k-aros-gcc
PYTHON     ?= python3

DATA ?= scumm-data/mi2demo
GAME ?= MI2DEMO
PACK_SRC = $(DATA)/$(GAME).000 $(wildcard $(DATA)/$(GAME).00[1-9])

# -mcpu=68020 (not -m68020) is what selects the m68020 libgcc multilib.
CFLAGS  = -mcpu=68020 -Os -ffreestanding -fno-builtin -fomit-frame-pointer \
          -Wall -Wextra -std=gnu99 -fno-common
LDFLAGS = -nostdlib -nostartfiles -r

OUT = build/amiga
PROGS = $(OUT)/roomview $(OUT)/restest
COMMON = $(OUT)/start.o $(OUT)/util.o $(OUT)/res.o $(OUT)/heap.o
PACK = $(OUT)/$(shell echo $(GAME) | tr A-Z a-z).s5p

all: $(PROGS) $(PACK)

$(OUT):
	mkdir -p $@

$(OUT)/%.o: src/%.c $(wildcard src/*.h) | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: src/%.s | $(OUT)
	$(CC) -mcpu=68020 -c $< -o $@

$(OUT)/%.elf: $(OUT)/%.o $(COMMON)
	$(CC) $(LDFLAGS) $(COMMON) $< -lgcc -o $@

$(OUT)/%: $(OUT)/%.elf
	$(ELF2HUNK) $< $@

$(PACK): tools/mkpack.py tools/room2planar.py $(PACK_SRC) | $(OUT)
	$(PYTHON) -I tools/mkpack.py $(PACK_SRC) -o $@

clean:
	rm -rf build

.PHONY: all clean
.DELETE_ON_ERROR:
.SECONDARY:
