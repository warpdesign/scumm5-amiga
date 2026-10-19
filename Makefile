# scumm5-amiga
# SPDX-License-Identifier: GPL-3.0-or-later

AROS_BUILD ?= $(HOME)/Documents/Dev/AROS-build
TOOLCHAIN  ?= $(AROS_BUILD)/toolchain
ELF2HUNK   ?= $(AROS_BUILD)/m68k/bin/darwin-aarch64/tools/elf2hunk
CC          = $(TOOLCHAIN)/m68k-aros-gcc
PYTHON     ?= python3

DATA ?= scumm-data/mi2demo
ROOM ?= 7
ROOM_FILE ?= $(DATA)/MI2DEMO.001

# -mcpu=68020 (not -m68020) is what selects the m68020 libgcc multilib.
CFLAGS  = -mcpu=68020 -Os -ffreestanding -fno-builtin -fomit-frame-pointer \
          -Wall -Wextra -std=gnu99 -fno-common
LDFLAGS = -nostdlib -nostartfiles -r

OUT = build/amiga
OBJS = $(OUT)/start.o $(OUT)/roomview.o

all: $(OUT)/roomview $(OUT)/room$(ROOM).pln

$(OUT):
	mkdir -p $@

$(OUT)/%.o: src/%.c src/amiga.h | $(OUT)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT)/%.o: src/%.s | $(OUT)
	$(CC) -mcpu=68020 -c $< -o $@

$(OUT)/roomview.elf: $(OBJS)
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

$(OUT)/roomview: $(OUT)/roomview.elf
	$(ELF2HUNK) $< $@

$(OUT)/room$(ROOM).pln: tools/room2planar.py $(ROOM_FILE) | $(OUT)
	$(PYTHON) -I tools/room2planar.py $(ROOM_FILE) --room $(ROOM) -o $@ --png $(OUT)/room$(ROOM).png

clean:
	rm -rf build

.PHONY: all clean
.DELETE_ON_ERROR:
