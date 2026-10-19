#!/usr/bin/env python3
# scumm5-amiga - build an Amiga resource pack from SCUMM v5 DOS data files
#
# Index layout and sound variant selection follow ScummVM
# engines/scumm/resource.cpp (readIndexFile, readResTypeList, loadResource)
# and engines/scumm/sound.cpp (readSoundResource), ScummVM commit
# 96b42d42bc8533953bdd701db059cc33c839a9ff.
#
# ScummVM is the legal property of its developers, whose names
# are too numerous to list here. Please refer to the COPYRIGHT
# file distributed with the ScummVM source distribution.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""Build an Amiga resource pack (.s5p) from SCUMM v5 DOS data files.

Pack layout, all big-endian, blobs 4-byte aligned:
  0   char[4] 'S5PK'
  4   u16     version (2)
  6   u16     number of resource types (5)
  8   5 x {u16 count, u16 0, u32 directory offset}
      types: 0 room, 1 script, 2 costume, 3 sound, 4 charset
  48  u32     room background directory offset (rooms count entries)
  52  u32     index blob offset, u32 index blob size
  directory entry:  u32 offset, u32 size, u32 checksum (all 0: absent)
  background entry: u32 offset, u32 size, u32 checksum,
                    u16 padded width, u16 height, u16 width, u16 0,
                    u32 RMIM offset and u32 RMIM size in the original ROOM
                    block (lets traces report original script offsets)

Resources are stored un-XORed and byte-identical to what ScummVM holds in
memory, except: a room is its ROOM block without RMIM (the background
lives in the background directory, as interleaved 8-bitplane rows padded
to 64 px); a sound is the one SOU sub-block ScummVM picks for General MIDI
output. The index blob holds the MAXS and DOBJ blocks of the index file.
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from room2planar import blocks, decode_room, load, rooms  # noqa: E402

TYPES = ('DROO', 'DSCR', 'DCOS', 'DSOU', 'DCHR')
TAGS = (b'ROOM', b'SCRP', b'COST', b'SOUN', b'CHAR')
NAMES = ('room', 'script', 'costume', 'sound', 'charset')
HEADER_SIZE = 60
PAD = 64  # planar rows padded for AGA 4x fetch

# readSoundResource priorities with General MIDI output (_musicType MDT_MIDI)
SOUND_PRI = {b'TOWS': 16, b'SBL ': 15, b'GMD ': 4, b'ROL ': 3, b'AMI ': 3,
             b'MAC ': 2, b'ADL ': 1}


def checksum(data):
    """Adler-style sum without modulo; matches res_checksum() in src/res.c."""
    a, c = 1, 0
    for b in data:
        a = (a + b) & 0xFFFFFFFF
        c = (c + a) & 0xFFFFFFFF
    return (c << 16 ^ a) & 0xFFFFFFFF


def read_index(idx):
    lists, keep = {}, []
    for tag, p, size in blocks(idx, 0, len(idx)):
        if tag in TYPES:
            num = struct.unpack_from('<H', idx, p + 8)[0]
            rn = idx[p + 10:p + 10 + num]
            offs = struct.unpack_from('<%dI' % num, idx, p + 10 + num)
            lists[tag] = list(zip(rn, offs))
        elif tag in ('MAXS', 'DOBJ'):
            keep.append(idx[p:p + size])
    return lists, b''.join(keep)


def strip_rmim(data, off):
    """ROOM block without RMIM, plus RMIM's offset and size in the original."""
    size = struct.unpack_from('>I', data, off + 4)[0]
    parts, rmim = [], (0, 0)
    for t, p, s in blocks(data, off + 8, off + size):
        if t == 'RMIM':
            rmim = (p - off, s)
        else:
            parts.append(data[p:p + s])
    body = b''.join(parts)
    return b'ROOM' + struct.pack('>I', len(body) + 8) + body, rmim


def pick_sound(data, off):
    size = struct.unpack_from('>I', data, off + 4)[0]
    base = data[off + 8:off + 12]
    if base in (b'MIDI', b'iMUS'):
        total = struct.unpack_from('>I', data, off + 12)[0]
        return data[off + 8:off + 16 + total]
    if base != b'SOU ':
        return data[off:off + size]
    total = struct.unpack_from('>I', data, off + 12)[0]
    p, best, best_pri = off + 16, None, -1
    while p < off + 16 + total:
        tag = data[p:p + 4]
        sub = struct.unpack_from('>I', data, p + 4)[0] + 8
        pri = SOUND_PRI.get(tag, -1)
        if pri > best_pri:
            best, best_pri = data[p:p + sub], pri
        p += sub
    return best


def planar_table():
    """table[pos][value]: the 8 plane bytes of one pixel, as a 64-bit int."""
    t = []
    for pos in range(8):
        row = []
        for v in range(256):
            x = 0
            for plane in range(8):
                if v >> plane & 1:
                    x |= (0x80 >> pos) << (8 * (7 - plane))
            row.append(x)
        t.append(row)
    return t


def to_planar(width, height, pixels, table):
    pw = (width + PAD - 1) // PAD * PAD
    rowbytes = pw // 8
    out = bytearray(rowbytes * 8 * height)
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        base = y * 8 * rowbytes
        for xb in range(width // 8):
            g = row[xb * 8:xb * 8 + 8]
            v = (table[0][g[0]] | table[1][g[1]] | table[2][g[2]] | table[3][g[3]] |
                 table[4][g[4]] | table[5][g[5]] | table[6][g[6]] | table[7][g[7]])
            bs = v.to_bytes(8, 'big')
            for plane in range(8):
                out[base + plane * rowbytes + xb] = bs[plane]
    return pw, bytes(out)


class Writer:
    def __init__(self):
        self.buf = bytearray(HEADER_SIZE)

    def blob(self, data):
        while len(self.buf) % 4:
            self.buf.append(0)
        off = len(self.buf)
        self.buf += data
        return off


def build(index_path, data_paths, verbose):
    lists, index_blob = read_index(load(index_path))
    files, roomloc = {}, {}
    for path in data_paths:
        disk = int(os.path.splitext(path)[1][1:])
        files[disk] = load(path)
        for room, off in rooms(files[disk]):
            roomloc[room] = (disk, off)

    w = Writer()
    table = planar_table()
    dirs, totals = [], [0] * (len(TYPES) + 1)
    bg_dir = bytearray()
    nroom = len(lists['DROO'])

    for t, tag in enumerate(TYPES):
        entries = bytearray()
        for i, (rn, roff) in enumerate(lists[tag]):
            if t == 0:
                rn, roff = i, 0
            loc = roomloc.get(rn)
            if loc is None or (t > 0 and rn == 0 and roff == 0):
                entries += bytes(12)
                if t == 0:
                    bg_dir += bytes(28)
                continue
            data = files[loc[0]]
            off = loc[1] + roff
            if data[off:off + 4] != TAGS[t]:
                raise ValueError('%s %d: expected %s at %d, found %r' % (NAMES[t], i, TAGS[t], off, data[off:off + 4]))
            if t == 0:
                res, rmim = strip_rmim(data, off)
                width, height, _, pixels = decode_room(data, off)
                pw, planes = to_planar(width, height, pixels, table)
                bg_dir += struct.pack('>IIIHHHHII', w.blob(planes), len(planes), checksum(planes), pw, height, width, 0,
                                      rmim[0], rmim[1])
                totals[-1] += len(planes)
            elif t == 3:
                res = pick_sound(data, off)
                if res is None:
                    entries += bytes(12)
                    continue
            else:
                res = data[off:off + struct.unpack_from('>I', data, off + 4)[0]]
            entries += struct.pack('>III', w.blob(res), len(res), checksum(res))
            totals[t] += len(res)
            if verbose:
                print('%-7s %3d  %6d bytes  %s' % (NAMES[t], i, len(res), res[:4].decode('latin-1')))
        dirs.append((len(lists[tag]), w.blob(bytes(entries))))

    bg_off = w.blob(bytes(bg_dir))
    idx_off = w.blob(index_blob)
    hdr = b'S5PK' + struct.pack('>HH', 2, len(TYPES))
    for count, off in dirs:
        hdr += struct.pack('>HHI', count, 0, off)
    hdr += struct.pack('>III', bg_off, idx_off, len(index_blob))
    assert len(hdr) == HEADER_SIZE and nroom == dirs[0][0]
    w.buf[0:HEADER_SIZE] = hdr
    return bytes(w.buf), totals


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('index', help='index file (e.g. MI2DEMO.000)')
    ap.add_argument('data', nargs='+', help='data files (e.g. MI2DEMO.001 MI2DEMO.002)')
    ap.add_argument('-o', '--output', required=True, help='output pack')
    ap.add_argument('-v', '--verbose', action='store_true', help='list every resource')
    args = ap.parse_args()

    pack, totals = build(args.index, args.data, args.verbose)
    with open(args.output, 'wb') as f:
        f.write(pack)
    print('%s: %d bytes (rooms %d, backgrounds %d, scripts %d, costumes %d, sounds %d, charsets %d)'
          % (args.output, len(pack), totals[0], totals[5], totals[1], totals[2], totals[3], totals[4]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
