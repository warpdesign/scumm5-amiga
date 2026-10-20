# scumm5-amiga

A SCUMM v5 interpreter for AGA Amigas, adapted from ScummVM, that makes the
games playable on a stock A1200 with just 2 MB of chip RAM.

Repository: https://github.com/warpdesign/scumm5-amiga

## Requirements

- AGA Amiga (68020+)
- 2 MB chip RAM
- Kickstart 3.1 or AROS 68k
- asl.library

## Usage

1. Copy the release drawer to the hard disk and start `scumm5`.
2. Choose "Add game..." and select the drawer holding the DOS game files.
   The game is converted once (about 8 minutes for Monkey Island 2) into
   `Games/<game>` next to `scumm5`; the DOS files are not needed afterwards.
3. Select the game and choose "Start".

In game: Esc skips a cutscene, F5 opens the save/load menu, Ctrl-C or
Alt-X quits.

## Supported games

| Game                                           | ScummVM MD5 (`.000`)               |
|------------------------------------------------|------------------------------------|
| Monkey Island 2 (DOS, English)                 | `3686cf8f89e102ececf4366e1d2c8126` |
| Monkey Island 2 demo (DOS)                     | `387a544b8b10b26912d8413bab63a853` |
| Fate of Atlantis (DOS floppy, experimental)    | `c63ee46143ba65f9ce14cf539ca51bd7` |

## Technical info

- **Engine.** ScummVM's v5 code (scripts, objects, actors, walk boxes,
  costumes, camera, verbs, text, iMUSE, save/load), checked opcode for
  opcode against ScummVM traces. Scrolling is in 8 px strips, as on DOS.
- **Game data.** The DOS files are converted once into a pack: un-XORed,
  index and data merged, images turned into 8-bitplane ByteRun1 strips,
  only the General MIDI variant of each sound kept, checksums added.
  `tools/mkpack.py` does it on a PC, the launcher on the Amiga
  (`src/convert.c`, same output). Resources load on demand into a chip RAM
  cache with ScummVM's expiry rules.
- **Display.** 320x200, 8 AGA bitplanes, 4x fetch, two display buffers
  flipped at vertical blank. Copper for the palette (only changed colours
  rewritten), hardware sprite for the pointer, blitter for background
  copies. Image unpacking, costume drawing and text are in 68k assembly.
- **Music.** iMUSE plays the games' MIDI tracks (MT-32 programs mapped to
  General MIDI) from a 100 Hz CIA-B timer. Each note plays a sample on one
  of the four Paula voices, pitch and volume set by the hardware registers.
  Samples come from a per-game bank built from the FluidR3_GM SoundFont
  (`tools/mkbank.py`), loaded into a sample pool as notes need them.
- **Platform.** No libc: own startup and inline OS calls (`src/amiga.h`),
  linked as relocatable ELF and converted to hunk format. Runs on
  Kickstart 3.x and AROS 68k. Portable code goes through `src/platform.h`, so the
  interpreter also builds for the host, with AddressSanitizer.

## Memory budget

With 2 MB of chip RAM and Workbench running:

| Item                               | Size                              |
|------------------------------------|-----------------------------------|
| Program                            | 200 KB                            |
| Display buffers and copper lists   | 135 KB                            |
| Instrument sample pool             | 256 KB                            |
| Resource cache                     | largest free block minus 856 KB   |
| Room screens, masks, OS            | the rest                          |

## Building

Needs the AROS m68k cross toolchain with `elf2hunk` (`AROS_BUILD`,
`TOOLCHAIN`, `ELF2HUNK` in the `Makefile`), a host C compiler, Python 3 and,
for instrument banks, numpy and the FluidR3_GM SoundFont (`SF2`).

    make                                  # everything below
    make DATA=path/to/monkey2 GAME=MONKEY2  # pack and bank of another game

What it builds:

| Output                     | What                                          |
|----------------------------|-----------------------------------------------|
| `build/amiga/scumm5`       | the launcher and game player                  |
| `build/amiga/scummrun`     | headless interpreter, for traces and checksums |
| `build/amiga/scummconv`    | command-line DOS-to-pack converter            |
| `build/amiga/roomview`     | shows one room background                     |
| `build/amiga/restest`      | resource loader test                          |
| `build/host/scummrun`      | host build of `scummrun`                      |
| `build/host/scummconv`     | host build of `scummconv`                     |
| `build/host/restest`       | host build of `restest`                       |
| `build/amiga/<game>.s5p`   | game pack, from `$(DATA)/$(GAME).0*`          |
| `build/amiga/<game>.s5b`   | instrument bank (only if `SF2` exists)        |

`DATA` and `GAME` default to `scumm-data/mi2demo` and `MI2DEMO`. The scripts
in `scripts/` run the test programs under Copperline and compare our traces
with ScummVM's.

To play, put `scumm5` in a drawer with a `Banks` drawer holding the
`<game>.s5b` files, then install games from the launcher.

## Why not port ScummVM

A SCUMM-only ScummVM build is about 2.9 MB of code before any game data,
more than the A1200 has. This project keeps only the SCUMM v5 engine, ported
from ScummVM to C, and replaces the rest with code written for the Amiga.

## TODO

- More optimizations
- Support talkie versions
- Support other SCUMM v4/v5 games

## Licence

GPLv3 or later (see `LICENSE`). Parts are adapted from ScummVM, which makes
this a derivative work; those files keep ScummVM's copyright notice and name
their source file and ScummVM commit.
