#!/usr/bin/env python3
# scumm5-amiga - SCUMM v5 room background -> AGA 8-bitplane converter
#
# Strip decoders adapted from ScummVM engines/scumm/gfx.cpp
# (Gdi::drawStripBasicH/V, drawStripRaw, MajMinCodec), ScummVM commit
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
"""Convert a SCUMM v5 room background to an interleaved 8-bitplane file.

Output (.pln), all big-endian:
  char[4]  'PLN8'
  u16      width   (padded to a multiple of 64 pixels, for AGA 4x fetch)
  u16      height
  u16      room width (unpadded)
  u16      room number
  u8[768]  palette, 8-bit R,G,B per colour
  planes   interleaved: for each row, plane 0..7, width/8 bytes each
"""

import argparse
import struct
import sys
import zlib

XOR_KEY = 0x69
PAD = 64


def load(path):
    with open(path, 'rb') as f:
        return bytes(b ^ XOR_KEY for b in f.read())


def blocks(data, start, end):
    p = start
    while p + 8 <= end:
        tag = data[p:p + 4].decode('latin-1')
        size = struct.unpack_from('>I', data, p + 4)[0]
        if size < 8:
            raise ValueError('bad block %r at %d' % (tag, p))
        yield tag, p, size
        p += size


def child(data, start, size, tag):
    for t, p, s in blocks(data, start + 8, start + size):
        if t == tag:
            return p, s
    raise KeyError(tag)


def rooms(data):
    """Yield (room number, ROOM block offset) from the LOFF table."""
    _, lecf_size = data[0:4], struct.unpack_from('>I', data, 4)[0]
    loff, _ = child(data, 0, lecf_size, 'LOFF')
    count = data[loff + 8]
    for i in range(count):
        num, off = struct.unpack_from('<BI', data, loff + 9 + i * 5)
        yield num, off


class Bits:
    """LSB-first bit reader matching the FILL_BITS/READ_BIT macros."""

    def __init__(self, src, pos, bits, count):
        self.src, self.pos, self.bits, self.count = src, pos, bits, count

    def fill(self):
        if self.count <= 8:
            self.bits |= self.src[self.pos] << self.count
            self.pos += 1
            self.count += 8

    def read(self, n):
        v = self.bits & ((1 << n) - 1)
        self.bits >>= n
        self.count -= n
        return v


def strip_basic(src, pos, height, shift, vertical):
    out = []
    color = src[pos]
    b = Bits(src, pos + 2, src[pos + 1], 8)
    mask = 0xFF >> (8 - shift)
    inc = -1
    for _ in range(8 * height):
        b.fill()
        out.append(color)
        if not b.read(1):
            pass
        elif not b.read(1):
            b.fill()
            color = b.read(shift) & mask
            inc = -1
        elif not b.read(1):
            color = (color + inc) & 0xFF
        else:
            inc = -inc
            color = (color + inc) & 0xFF
    if vertical:
        return [out[x * height + y] for y in range(height) for x in range(8)]
    return out


def strip_majmin(src, pos, height, shift):
    color = src[pos]
    b = Bits(src, pos + 3, src[pos + 1] | src[pos + 2] << 8, 16)

    def rd(n):
        b.fill()
        return b.read(n)

    out = []
    repeat = 0
    for _ in range(8 * height):
        out.append(color)
        if repeat:
            repeat -= 1
        elif rd(1):
            if rd(1):
                diff = rd(3) - 4
                if diff:
                    color = (color + diff) & 0xFF
                else:
                    repeat = (rd(8) - 1) & 0xFF
            else:
                color = rd(shift)
    return out


def decode_strip(src, pos, height):
    code = src[pos]
    pos += 1
    shift = code % 10
    if code == 1:
        return list(src[pos:pos + 8 * height])
    if 14 <= code <= 18 or 34 <= code <= 38:
        return strip_basic(src, pos, height, shift, True)
    if 24 <= code <= 28 or 44 <= code <= 48:
        return strip_basic(src, pos, height, shift, False)
    if 64 <= code <= 68 or 84 <= code <= 88 or 104 <= code <= 108 or 124 <= code <= 128:
        return strip_majmin(src, pos, height, shift)
    raise ValueError('unsupported strip codec %d' % code)


def decode_room(data, room_off):
    room_size = struct.unpack_from('>I', data, room_off + 4)[0]
    rmhd, _ = child(data, room_off, room_size, 'RMHD')
    width, height = struct.unpack_from('<HH', data, rmhd + 8)
    clut, _ = child(data, room_off, room_size, 'CLUT')
    palette = data[clut + 8:clut + 8 + 768]
    rmim, rmim_size = child(data, room_off, room_size, 'RMIM')
    im00, im00_size = child(data, rmim, rmim_size, 'IM00')
    smap, _ = child(data, im00, im00_size, 'SMAP')
    pixels = bytearray(width * height)
    for s in range(width // 8):
        off = struct.unpack_from('<I', data, smap + 8 + s * 4)[0]
        strip = decode_strip(data, smap + off, height)
        for y in range(height):
            pixels[y * width + s * 8:y * width + s * 8 + 8] = bytes(strip[y * 8:y * 8 + 8])
    return width, height, palette, pixels


def to_planar(width, height, pixels):
    pw = (width + PAD - 1) // PAD * PAD
    rowbytes = pw // 8
    out = bytearray(rowbytes * 8 * height)
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        for p in range(8):
            base = (y * 8 + p) * rowbytes
            bit = 1 << p
            for xb in range(width // 8):
                v = 0
                for i in range(8):
                    if row[xb * 8 + i] & bit:
                        v |= 0x80 >> i
                out[base + xb] = v
    return pw, out


def write_png(path, width, height, palette, pixels):
    raw = b''.join(b'\0' + bytes(pixels[y * width:(y + 1) * width]) for y in range(height))

    def chunk(tag, body):
        c = tag + body
        return struct.pack('>I', len(body)) + c + struct.pack('>I', zlib.crc32(c))

    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n')
        f.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 3, 0, 0, 0)))
        f.write(chunk(b'PLTE', bytes(palette)))
        f.write(chunk(b'IDAT', zlib.compress(raw, 9)))
        f.write(chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('datafile', help='SCUMM v5 data file (e.g. MI2DEMO.001)')
    ap.add_argument('--list', action='store_true', help='list rooms and sizes')
    ap.add_argument('--room', type=int, help='room number to convert')
    ap.add_argument('-o', '--output', help='output .pln file')
    ap.add_argument('--png', help='also write a chunky PNG preview')
    args = ap.parse_args()

    data = load(args.datafile)
    table = dict(rooms(data))

    if args.list:
        for num, off in sorted(table.items()):
            size = struct.unpack_from('>I', data, off + 4)[0]
            rmhd, _ = child(data, off, size, 'RMHD')
            w, h, nobj = struct.unpack_from('<HHH', data, rmhd + 8)
            print('room %3d  %4dx%-3d  objects %3d  size %7d' % (num, w, h, nobj, size))
        return 0

    if args.room is None or args.room not in table:
        ap.error('--room must be one of %s' % sorted(table))

    width, height, palette, pixels = decode_room(data, table[args.room])
    if args.png:
        write_png(args.png, width, height, palette, pixels)
    if args.output:
        pw, planes = to_planar(width, height, pixels)
        with open(args.output, 'wb') as f:
            f.write(b'PLN8' + struct.pack('>HHHH', pw, height, width, args.room))
            f.write(bytes(palette))
            f.write(planes)
    print('room %d: %dx%d' % (args.room, width, height))
    return 0


if __name__ == '__main__':
    sys.exit(main())
