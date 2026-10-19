# scumm5-amiga

A dedicated, planar-native SCUMM v5 interpreter for an unexpanded Amiga A1200.

Target games (same interpreter, both SCUMM v5, 256-colour VGA, iMUSE music):

1. **Monkey Island 2, DOS** (first). Requirement: 256-colour graphics + music.
2. **Indiana Jones and the Fate of Atlantis, DOS CD talkie** (second), and
   the target for speech support. The fan "Ultimate Talkie" MI2 edition
   could be covered by the same speech support later.

Priority: MI2 graphics + music first; speech later.

## Target hardware

- Amiga A1200: AGA chipset, 68EC020 @ 14 MHz
- 2 MB chip RAM, **no fast RAM** (CPU, blitter, copper, bitplane DMA and
  Paula all share the chip bus)
- Kickstart 3.x (development/testing also under AROS m68k and the Copperline emulator)

## Why not port ScummVM

A SCUMM-only ScummVM build (v7/v8, HE and optional libs disabled), compiled
`-Os -m68020` with `--gc-sections`, is 2.87 MB of code+rodata before libc,
backend or data: 1.64 MB framework (GUI alone 568 KB) + 1.21 MB SCUMM engine.
The v5-relevant engine files alone are ~408 KB, still carrying other-version
paths. A clean v5-only interpreter is estimated at ~250-400 KB of code.

## Memory budget (2 MB chip, rough, to be measured)

MI2 layout: 320x144 room viewport + verb/inventory area below. At 8 bitplanes
one byte per pixel of planar data: a 320x200 screen is 64 KB, a 320x144 room
strip is 45 KB per 320 px of room width.

| Item                                   | Estimate      | Notes |
|----------------------------------------|---------------|-------|
| Interpreter code                       | ~300 KB       | core without drawing/iMUSE: 70 KB |
| Resource cache                         | ~400-550 KB   | MI2 demo: 400 KB arena peaks at 385 KB (milestone 4) |
| Room playfield (8 bpl, wide)           | ~90-120 KB    | measured: demo room 7, 784x144 padded to 832 = 117 KB |
| Clean background copy (actor restore)  | ~90-120 KB    | same size as playfield; could be avoided by re-decoding strips |
| Z-plane masks (1 bpl each)             | ~10-35 KB     | |
| Verb/inventory area (320x56x8)         | ~18 KB        | |
| Instrument samples                     | ~100-150 KB   | FluidR3_GM subset, 8-bit, see Music |
| Audio buffers, copper lists, stacks    | ~30 KB        | |
| Speech stream buffer (later)           | ~16-32 KB     | double-buffered, streamed from disk |
| OS                                     | remainder     | AROS m68k uses more than Kickstart 3.1 |

The MI2 demo's widest room is 784 px; its rooms are 144 lines high (200 for
full-screen ones). Planar rows are padded to 64 px for AGA 4x fetch.

Total for a 640 px room: ~1.1-1.3 MB before the OS. Fits, but much less slack
than a 5-bitplane design; wide rooms and the resource cache are the knobs.

## Design direction

- **Offline conversion.** Host-side tools convert the DOS resources into
  Amiga-friendly formats (planar bitplanes, blitter masks, sample banks),
  keeping SCUMM scripts and semantics unchanged.
- **Graphics.** 256 colours = 8 AGA bitplanes. AGA 24-bit palette (copper
  colour writes / LoadRGB32-style) covers the VGA 18-bit palette, including
  script-driven palette changes. Blitter cookie-cut for actors and objects
  (8 planes per blit); copper for palette and the room/verb-area split;
  hardware scrolling (bitplane pointers + BPLCON1 over a wide playfield)
  instead of strip redraws. Avoid CPU pixel loops.
- **Music.** The DOS data has no sampled instruments: its iMUSE sounds hold
  MIDI-style tracks for MT-32/GM, AdLib and PC speaker (formats: ScummVM
  `engines/scumm/imuse/`, `sound.cpp`). OPL emulation is too heavy for a
  14 MHz 020, and pre-rendered audio loses iMUSE interactivity and is too big.
  Direction: an iMUSE sequencer playing the DOS MIDI tracks through a
  sample-based Paula driver (4 hardware voices, maybe a few software-mixed
  extra voices if CPU allows), with an offline tool mapping MT-32/GM programs
  to a compact 8-bit instrument set.
- **Instrument source: FluidR3_GM** (MIT licence, so the derived bank is
  redistributable). MT-32 programs are mapped to GM; some of the MT-32
  character is lost, which is accepted. Budget-driven rules, since 2 MB chip
  RAM and a 14 MHz 020 with no fast RAM leave little room:
  - offline: keep only the programs and key ranges the game's tracks use,
    one (at most two) sample zones per instrument, 8-bit, ~8-11 kHz, short
    loops, cropped attacks; whole bank ~100-150 KB, loaded once;
  - runtime: pitch by Paula period and volume by Paula volume registers, so
    no per-sample CPU work; envelopes and the sequencer run once per frame
    (VBL); MIDI channels are allocated onto the 4 hardware voices by
    priority with voice stealing. Software mixing only if measured CPU
    headroom allows it;
  - the bank format is independent of its source, so another bank (e.g. one
    sampled from an MT-32 for personal builds) can be swapped in.
- **Speech (later, Atlantis CD).** Speech lives in a separate `MONSTER.SOU`
  bank of Creative VOC samples, triggered by scripts alongside dialogue
  (ScummVM `engines/scumm/sound.cpp`). VOC is 8-bit, so it suits Paula; an
  offline tool may resample/compress it. The file is too large for RAM, so
  speech streams from hard disk/CD. Constraints to respect from the start:
  keep one Paula voice (or a mixer slot) available for a voice stream, keep
  the disk I/O path asynchronous and non-blocking, and keep a small streaming
  buffer in the memory budget.
- **Language.** C for the interpreter, 68k assembly for hot paths.

## Risks / open questions

- **Bitplane DMA bandwidth.** 8 lowres bitplanes at 1x fetch take every DMA
  slot during the display window, starving CPU and blitter. Need FMODE 2x/4x
  fetch, with its alignment and scrolling constraints.
- **Costume palettes.** v5 costumes index a per-actor palette remap; planar
  pre-conversion must handle remaps (variants, or plane-wise tricks).
- **Instrument samples.** FluidR3_GM chosen; fitting a usable GM subset into
  ~100-150 KB at 8 bits, and 16 MIDI channels onto 4 voices, needs testing.
- **Speech vs. music voices.** Giving a voice to speech leaves 3 hardware
  voices for music while someone talks (or forces software mixing).
- **Streaming bandwidth.** Speech reads compete with resource loading on the
  same disk; rate, buffering and seek patterns need measuring.
- **Cache fragmentation by locked resources.** The current room and running
  scripts cannot be expired; in a small arena they split the free space so a
  large load fails even though enough bytes are free (seen below ~256 KB in
  `restest`). Compaction was tried and removed: under random access it ran on
  half of all loads without reducing expiries. Revisit with real interpreter
  access patterns, and with the pinned set of a real scene (visible actors'
  costumes, playing music) measured.
- **Room size without the background.** Demo rooms are still 30-60 KB in the
  cache, mostly object images (OBIM, same strip codecs); converting those to
  planar changes the figure.
- **Atlantis specifics.** Its resource sizes and room widths may differ from
  MI2; re-check the memory budget before starting on it.

## Layout

- `src/`        platform layer, resource loader, test programs
- `src/scumm/`  the SCUMM v5 interpreter (portable C)
- `tools/`      host-side converters and the trace comparer
- `scripts/`    build, Copperline and trace scripts

## Building and running

Needs the AROS m68k cross toolchain and `elf2hunk` (paths in `Makefile`,
override `AROS_BUILD`), Python 3, and Copperline.

    make                              # programs + build/amiga/mi2demo.s5p
    scripts/run-roomview.sh 8         # show demo room 8, LMB exits
    scripts/run-roomview.sh 7 out.png # headless screenshot
    ROOMVIEW_ARGS="0 100" scripts/run-roomview.sh 7   # hold room column 100
    scripts/run-restest.sh [ARENA_KB [ACCESSES]]      # resource loader test
    build/host/scummrun build/amiga/mi2demo.s5p 2000 -r scumm-data/mi2demo/DEMO.REC
    scripts/scummvm-trace.sh ref.txt 600              # ScummVM reference trace
    scripts/trace-compare.sh ref.txt 6000             # our trace against it

Rooms wider than 320 px scroll back and forth, 1 px per frame. `make` builds
the resource pack from `$(DATA)/$(GAME).0*` (defaults: `scumm-data/mi2demo`,
`MI2DEMO`) with `tools/mkpack.py`; its docstring describes the pack format.

Amiga programs are freestanding (no libc, own startup and inline OS calls
in `src/amiga.h`), linked as relocatable ELF and converted to hunk format,
so they run on Kickstart 3.x as well as AROS. Portable code goes through
`src/platform.h`; `make` also builds host versions (`build/host/`, with
AddressSanitizer) of the programs that do not touch the hardware.

`scummrun PACK [FRAMES] [-t TRACE] [-r DEMO.REC] [-a ARENA_KB]` runs the
interpreter without display or sound, optionally writing a trace in
ScummVM's `--debugflags=OPCODES,SCRIPTS` format. The reference script needs
a SCUMM-enabled ScummVM build (`$SCUMMVM`), built out of tree, for example:
`../scummvm/configure --disable-all-engines --enable-engine=scumm` in an
empty directory.

`tools/room2planar.py DATAFILE --list` lists rooms; `--room N --png F.png`
writes one background as a PNG (demo rooms 2-32 are in `MI2DEMO.001`, 46-93
in `MI2DEMO.002`).

## Status

Milestone 1 done: a DOS MI2 demo room converted offline to interleaved
8-bitplane data, shown under Copperline (A1200, 2 MB chip, no fast RAM,
bundled AROS ROM) with a copper-loaded 24-bit palette and FMODE=3 fetch,
pixel-exact against the decoded source. Notes:

- With FMODE=3 and DDFSTRT/DDFSTOP $38/$D0 the hardware fetches 6 blocks of
  64 px (48 bytes) per plane per line, not 40: modulos must account for it.
- After `LoadView(NULL)` under AROS, bitplane DMA was off; set it in DMACON.
- Not yet tested on a real Kickstart 3.1 A1200 ROM (none available here).

Milestone 2 done: hardware horizontal scrolling over the full width of demo
room 7 (784 px), any pixel position, checked pixel-exact at columns 0, 1, 63,
64, 100, 464 and on a frame captured mid-scroll. How it works:

- BPLCON1 delays the picture by 0-63 px (AGA PF1H/PF2H bits 2-7); bitplane
  pointers move in 64 px steps, as 4x fetch needs 8-byte-aligned pointers.
- DDFSTRT is $18, one 64 px block before $38, so the delayed left edge has
  fetched data; that makes 7 blocks (56 bytes) per plane per line. Sprite
  DMA slots are lost, which is fine as sprites are not used.
- The plane buffer has 8 bytes of padding in front and 16 behind for the
  extra block fetched at either end.
- Two copper lists: the CPU patches 17 words (BPLCON1 + 8 plane pointers) in
  the idle one after each vblank and points COP1LC at it, so a frame is
  never changed while it is displayed. Per-frame CPU cost is negligible.

Milestone 3 done: resource loader. `tools/mkpack.py` turns the index and
data files into one pack (un-XORed, big-endian directories, per-resource
checksums); `src/res.c` loads resources on demand into a fixed chip-RAM
arena with ScummVM-style least-recently-used expiry, locks and an in-use
veto for the interpreter. Notes:

- A room in the pack is its ROOM block without RMIM; the background is a
  separate planar record read straight into the playfield, so it is never
  held twice. Sounds keep only the variant ScummVM picks for General MIDI.
- Demo pack: 2.6 MB, of which 1.5 MB planar backgrounds; rooms 398 KB,
  costumes 452 KB, sounds 176 KB, scripts 52 KB, charsets 16 KB. Directory
  tables take 15 KB of RAM.
- The demo's music exists only as ROL (MT-32) tracks, plus two AdLib-only
  sounds: the music driver needs an MT-32 to GM program mapping.
- The heap allocates small blocks from the bottom of the arena and large
  ones (16 KB and up) from the top, so locked scripts do not split the room
  space; under `restest` that cut failures at 128 KB from 37 to 5.
- `restest` (every resource, then random accesses with the current room and
  scripts locked, checksums throughout, then every background) passes with a
  256 KB arena (4000 accesses) and 384 KB (20000).
- `roomview` now reads rooms through the loader, palette from the room's
  CLUT block.

Milestone 4 done: the SCUMM v5 interpreter core (`src/scumm/`, portable C
ported from ScummVM's v5 code with ScummVM's optional enhancements off),
running headless. It covers the script VM and all v5 opcodes, objects and
inventory, room entry, walk boxes and path finding, actor walking and
turning, costume animation and actor extents (for "actor under cursor"),
camera, verbs and clicks, message layout and timing, the sound command
queue, ScummVM's seeded random generator and the MI2 demo's `DEMO.REC`
input playback. Results:

- Opcode-for-opcode identical to ScummVM: all 144,775 trace lines of a
  10-minute ScummVM run of the MI2 demo (boot, intro, and the recorded demo
  through rooms 7, 14, 12, 51) match ours.
- The emulated A1200 (2 MB chip, no fast RAM) produces a byte-identical
  trace to the host build.
- The whole demo recording (8,284 frames) plays through without errors.
- Speed on the emulated 14 MHz 020, no display yet: 300 s of game time in
  64 s (about 21% CPU). The actor-extent pass is most of it; it becomes the
  costume renderer's sizing step later.
- The cache (400 KB arena) peaks at 385 KB; the program is 70 KB.

Not done yet: drawing (backgrounds per room are shown by `roomview` only;
objects, actors and text are state only), iMUSE (a sound counts as running
only while queued, so traces diverge where scripts wait on music; ScummVM
with a null driver still times songs in real time), the Amiga input path
(mouse/keys outside playback), save/load.

## License and provenance

GPLv3 or later (see `COPYING`). Parts are adapted from ScummVM, making this a
derivative work. Adapted files keep ScummVM copyright headers and record their
provenance (source file + ScummVM commit).

Game data is not included and must never be committed.

## Game data

Game data lives in `scumm-data/` (git-ignored). Current development set:

- `scumm-data/mi2demo/`: DOS MI2 demo (ScummVM "monkey2" "Demo", v5,
  `GF_DEMO`): `MI2DEMO.000` (index), `MI2DEMO.001`, `MI2DEMO.002`,
  `MI2DEMO.EXE`, `DEMO.REC`, `NULL.IMS`.
