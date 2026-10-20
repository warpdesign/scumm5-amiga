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

COMMON_SRC = util res heap convert detect
SCUMM_SRC = vm ops5 object room boxes actor costume camera verbs text sound imuse input engine trace gfx saveload menu
AMIGA_PROGS = roomview restest scummrun scumm5 scummconv
HOST_PROGS = restest scummrun scummconv

AMIGA_COMMON = $(OUT)/start.o $(OUT)/plat_amiga.o $(COMMON_SRC:%=$(OUT)/%.o)
HOST_COMMON = $(HOST)/plat_host.o $(COMMON_SRC:%=$(HOST)/%.o)

PACK = $(OUT)/$(shell echo $(GAME) | tr A-Z a-z).s5p
BANK = $(PACK:.s5p=.s5b)
# General MIDI SoundFont for the instrument bank (FluidR3_GM, MIT licence)
SF2 ?= $(HOME)/Documents/Dev/soundfonts/fluid-deb/x/usr/share/sounds/sf2/FluidR3_GM.sf2

all: $(AMIGA_PROGS:%=$(OUT)/%) $(HOST_PROGS:%=$(HOST)/%) $(PACK) $(if $(wildcard $(SF2)),$(BANK))

AMIGA_SCUMM = $(SCUMM_SRC:%=$(OUT)/scumm/%.o) $(OUT)/costume68k.o $(OUT)/text68k.o $(OUT)/gfx68k.o
HOST_SCUMM = $(SCUMM_SRC:%=$(HOST)/scumm/%.o)

$(OUT) $(HOST) $(OUT)/scumm $(HOST)/scumm:
	mkdir -p $@

# The interpreter is the hot code: -O2 rather than -Os.
$(OUT)/scumm/%.o: src/scumm/%.c $(HEADERS) | $(OUT)/scumm
	$(CC) $(CFLAGS) -O2 -c $< -o $@

$(HOST)/scumm/%.o: src/scumm/%.c $(HEADERS) | $(HOST)/scumm
	$(HOSTCC) $(HOSTCFLAGS) -c $< -o $@

$(OUT)/scummrun.elf: $(OUT)/scummrun.o $(AMIGA_COMMON) $(AMIGA_SCUMM) $(OUT)/scumm/display_null.o $(OUT)/scumm/audio_null.o
	$(CC) $(LDFLAGS) $(AMIGA_COMMON) $(AMIGA_SCUMM) $(OUT)/scumm/display_null.o $(OUT)/scumm/audio_null.o $< -lgcc -o $@

AMIGA_AUDIO = $(OUT)/audio_amiga.o $(OUT)/scumm/synth.o

$(OUT)/scumm5.elf: $(OUT)/scumm_main.o $(AMIGA_COMMON) $(AMIGA_SCUMM) $(OUT)/display_amiga.o $(OUT)/input_amiga.o $(OUT)/vbl.o $(OUT)/launcher.o $(AMIGA_AUDIO)
	$(CC) $(LDFLAGS) $(AMIGA_COMMON) $(AMIGA_SCUMM) $(OUT)/display_amiga.o $(OUT)/input_amiga.o $(OUT)/vbl.o $(OUT)/launcher.o $(AMIGA_AUDIO) $< -lgcc -o $@

HOST_AUDIO = $(HOST)/scumm/audio_null.o $(HOST)/scumm/synth.o $(HOST)/scumm/paula_host.o

$(HOST)/scummrun: $(HOST)/scummrun.o $(HOST_COMMON) $(HOST_SCUMM) $(HOST)/scumm/display_host.o $(HOST_AUDIO)
	$(HOSTCC) $(HOSTCFLAGS) $(HOST_COMMON) $(HOST_SCUMM) $(HOST)/scumm/display_host.o $(HOST_AUDIO) $< -o $@

# The converter's strip decoding is the slow part of installing a game.
$(OUT)/convert.o: CFLAGS += -O2
# The AROS headers' library call macros use register variables.
$(OUT)/launcher.o: CFLAGS += -Wno-volatile-register-var -I$(OUT)
$(OUT)/launcher.o: $(OUT)/version.h

VERSION ?= 0.0.2

# Shown by the launcher's About box; leave empty until the repository exists.
REPO_URL ?= https://github.com/warpdesign/scumm5-amiga

# Rewritten only when the version text changes, so the launcher is not
# rebuilt at every make. "+" after the hash: built with uncommitted changes.
$(OUT)/version.h: FORCE | $(OUT)
	@v=$$(git describe --always --dirty=+ 2>/dev/null || echo unknown); \
	printf '#define SCUMM5_VERSION "%s (%s, %s)"\n#define SCUMM5_REPO "%s"\n' "$(VERSION)" "$$v" "$$(date +%Y-%m-%d)" "$(REPO_URL)" > $@.tmp; \
	cmp -s $@.tmp $@ || mv $@.tmp $@; rm -f $@.tmp

.PHONY: FORCE
FORCE:

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

# mkbank needs numpy, so it runs without -I.
$(BANK): tools/mkbank.py tools/mkpack.py $(PACK_SRC) | $(OUT)
	$(PYTHON) tools/mkbank.py $(SF2) $(PACK_SRC) -o $@

$(PACK): tools/mkpack.py tools/room2planar.py $(PACK_SRC) | $(OUT)
	$(PYTHON) -I tools/mkpack.py $(PACK_SRC) -o $@

clean:
	rm -rf build

.PHONY: all clean
.DELETE_ON_ERROR:
.SECONDARY:
