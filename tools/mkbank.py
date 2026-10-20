#!/usr/bin/env python3
# scumm5-amiga - build a Paula instrument bank from a General MIDI SoundFont
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build an 8-bit Paula instrument bank (.s5b) from an SF2 General MIDI set.

Only the programs, drum notes and key ranges the game's music uses are kept:
the tool replays the program choices iMUSE makes on a General MIDI device
(MT-32 programs and custom timbres mapped to GM, LEC's rhythm map) over
every ROL/GMD track. Each kept zone is resampled to the lowest rate that
still lets its highest used note play within Paula's limit, capped by
--max-rate, and stored as signed 8-bit.

Bank layout, all big-endian:
  0   char[4] 'S5BK'
  4   u16     version (1)
  6   u16     number of zones
  8   u8[128] first zone of each GM program (0xFF: none)
  136 u8[128] number of zones of each GM program, sorted by key_hi
  264 u8[128] zone of each percussion key (0xFF: none)
  392 zones, 40 bytes each:
        u8  key_hi, root_key, exclusive_class, flags (bit 0: looped)
        s16 tune (1/64 semitone, added to the root's pitch)
        s16 attenuation (cB, normalisation included)
        u32 rate (Hz << 8)
        u32 sample offset (from the sample data start)
        u32 length (bytes, even), u32 loop start (bytes, even), u32 loop length
        u16 attack (10 ms ticks), u16 hold, u16 decay, u16 release
              (decay and release: ticks for 100 dB, as SF2 defines them)
        u16 sustain (cB below peak)
        u16 0
  then the sample data, signed 8-bit, each sample 2-byte aligned.
"""

import argparse
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkpack import load, pick_sound, read_index  # noqa: E402
from room2planar import rooms  # noqa: E402

PAULA_MAX_RATE = 28000		# PAL period 127: safely under the 1-word-per-line limit
TICK = 0.01
DYING_LOOP = 0.25		# loop cap (s) for notes that die away: heard quietly, if at all

MT32_TO_GM = (
    0, 1, 0, 2, 4, 4, 5, 3, 16, 17, 18, 16, 16, 19, 20, 21,
    6, 6, 6, 7, 7, 7, 8, 112, 62, 62, 63, 63, 38, 38, 39, 39,
    88, 95, 52, 98, 97, 99, 14, 54, 102, 96, 53, 102, 81, 100, 14, 80,
    48, 48, 49, 45, 41, 40, 42, 42, 43, 46, 45, 24, 25, 28, 27, 104,
    32, 32, 34, 33, 36, 37, 35, 35, 79, 73, 72, 72, 74, 75, 64, 65,
    66, 67, 71, 71, 68, 69, 70, 22, 56, 59, 57, 57, 60, 60, 58, 61,
    61, 11, 11, 98, 14, 9, 14, 13, 12, 107, 107, 77, 78, 78, 76, 76,
    47, 117, 127, 118, 118, 116, 115, 119, 115, 112, 55, 124, 123, 0, 14, 117)
GM_RHYTHM_MAP = [0] * 24 + [36, 37, 38, 39, 40, 41, 66, 47, 65, 48, 56]
ROLAND_TO_GM = {
    b'badspit   ': 62, b'Big Drum  ': 116, b'burp      ': 58, b'foghorn   ': 60,
    b'glop      ': 39, b'LeshBass  ': 33, b'ML explosn': 127, b'ReggaeBass': 32,
    b'rumble    ': 89, b'SdTrk Bend': 97, b'spitting  ': 62, b'Swell 1   ': 95,
    b'Swell 2   ': 95, b'thnderclap': 127}


# ---- what the music uses ----

def vlq(b, p):
    v = 0
    while True:
        c = b[p]
        p += 1
        v = v << 7 | (c & 0x7F)
        if not c & 0x80:
            return v, p


def decode_sysex(src):
    return bytes(((src[i] << 4) & 0xFF) | (src[i + 1] & 0xF) for i in range(0, len(src) - 1, 2))


def scan_track(b, p, end, mt32, melodic, drums):
    prog = [None] * 16
    perc = [c == 9 for c in range(16)]
    transpose = [0] * 16
    rs = 0
    while p < end:
        _, p = vlq(b, p)
        if b[p] & 0x80:
            ev = b[p]
            p += 1
        else:
            ev = rs
        if ev == 0xFF:
            ty = b[p]
            n, p = vlq(b, p + 1)
            p += n
            if ty == 0x2F:
                return
            continue
        if ev == 0xF0:
            n, p = vlq(b, p)
            s = b[p:p + n]
            p += n
            if s[0] == 0x7D and s[1] == 0:
                ch = s[2] & 15
                d = decode_sysex(s[3:])
                perc[ch] = bool(d[4] & 0x80)
                t = d[4] - 256 if d[4] > 127 else d[4]
                transpose[ch] = 0 if t == -128 else max(-12, min(12, t))
                if not perc[ch]:
                    prog[ch] = MT32_TO_GM[d[7]] if mt32 else d[7]
            elif s[0] == 0x41:
                gm = ROLAND_TO_GM.get(bytes(s[7:17]))
                if gm is not None:
                    prog[s[1] & 15] = gm
            continue
        rs = ev
        c, ch = ev >> 4, ev & 15
        if c in (0xC, 0xD):
            a = b[p]
            p += 1
            if c == 0xC and a < 128:
                prog[ch] = MT32_TO_GM[a] if mt32 else a
            continue
        a, v = b[p], b[p + 1]
        p += 2
        if c != 9 or not v:
            continue
        if perc[ch]:
            drums.add(GM_RHYTHM_MAP[a] if a < 35 else a)
        elif prog[ch] is not None:
            k = a + transpose[ch]
            lo, hi = melodic.get(prog[ch], (127, 0))
            melodic[prog[ch]] = (min(lo, k), max(hi, k))


def scan_music(index_path, data_paths):
    lists, _ = read_index(load(index_path))
    files, roomloc = {}, {}
    for path in data_paths:
        disk = int(os.path.splitext(path)[1][1:])
        files[disk] = load(path)
        for room, off in rooms(files[disk]):
            roomloc[room] = (disk, off)
    melodic, drums, per_sound = {}, set(), {}
    for sound, (rn, roff) in enumerate(lists['DSOU']):
        loc = roomloc.get(rn)
        if loc is None or (rn == 0 and roff == 0):
            continue
        b = pick_sound(files[loc[0]], loc[1] + roff)
        if b is None or b[:4] not in (b'ROL ', b'GMD ', b'MIDI'):
            continue
        p = b.find(b'MThd')
        if p < 0:
            continue
        ntr = struct.unpack_from('>H', b, p + 10)[0]
        p += 14
        sm, sd = {}, set()
        for _ in range(ntr):
            n = struct.unpack_from('>I', b, p + 4)[0]
            scan_track(b, p + 8, p + 8 + n, b[:4] == b'ROL ', sm, sd)
            p += 8 + n
        per_sound[sound] = (set(sm), sd)
        for prog, (lo, hi) in sm.items():
            glo, ghi = melodic.get(prog, (127, 0))
            melodic[prog] = (min(lo, glo), max(hi, ghi))
        drums |= sd
    return melodic, drums, per_sound, len(lists['DSOU'])


# ---- SoundFont ----

GEN_DEFAULTS = {8: 13500, 33: -12000, 34: -12000, 35: -12000, 36: -12000, 37: 0, 38: -12000,
                48: 0, 51: 0, 52: 0, 54: 0, 56: 100, 57: 0, 58: -1, 17: 0}
RANGE_GENS = (43, 44)
OFFSET_GENS = (0, 1, 2, 3, 4, 12, 45, 50)


class SoundFont:
    def __init__(self, path):
        data = open(path, 'rb').read()
        assert data[:4] == b'RIFF' and data[8:12] == b'sfbk'
        self.chunks = {}
        p = 12
        while p < len(data):
            tag, size = data[p:p + 4], struct.unpack_from('<I', data, p + 4)[0]
            if tag == b'LIST':
                q, end = p + 12, p + 8 + size
                while q < end:
                    t2, s2 = data[q:q + 4], struct.unpack_from('<I', data, q + 4)[0]
                    self.chunks[t2] = data[q + 8:q + 8 + s2]
                    q += 8 + s2 + (s2 & 1)
            p += 8 + size + (size & 1)
        self.smpl = np.frombuffer(self.chunks[b'smpl'], dtype='<i2')
        self.phdr = [struct.unpack_from('<20sHHHIII', self.chunks[b'phdr'], i) for i in range(0, len(self.chunks[b'phdr']), 38)]
        self.inst = [struct.unpack_from('<20sH', self.chunks[b'inst'], i) for i in range(0, len(self.chunks[b'inst']), 22)]
        self.pbag = [struct.unpack_from('<HH', self.chunks[b'pbag'], i) for i in range(0, len(self.chunks[b'pbag']), 4)]
        self.ibag = [struct.unpack_from('<HH', self.chunks[b'ibag'], i) for i in range(0, len(self.chunks[b'ibag']), 4)]
        self.pgen = self.gens(self.chunks[b'pgen'])
        self.igen = self.gens(self.chunks[b'igen'])
        self.shdr = [struct.unpack_from('<20sIIIIIBbHH', self.chunks[b'shdr'], i) for i in range(0, len(self.chunks[b'shdr']), 46)]

    @staticmethod
    def gens(chunk):
        out = []
        for i in range(0, len(chunk), 4):
            oper = struct.unpack_from('<H', chunk, i)[0]
            if oper in RANGE_GENS:
                out.append((oper, (chunk[i + 2], chunk[i + 3])))
            else:
                out.append((oper, struct.unpack_from('<h', chunk, i + 2)[0]))
        return out

    def zones(self, bags, gens, first, last, terminal):
        """List of generator dicts; a leading zone without the terminal
        generator is global and merged into the others."""
        out, glob = [], {}
        for b in range(first, last):
            g = dict(gens[bags[b][0]:bags[b + 1][0]])
            if terminal not in g:
                if b == first:
                    glob = g
                continue
            z = dict(glob)
            z.update(g)
            out.append(z)
        return out

    def preset_zones(self, bank, program):
        """Instrument zones of a preset, with preset generators added."""
        for i, h in enumerate(self.phdr[:-1]):
            if h[1] == program and h[2] == bank:
                break
        else:
            return []
        result = []
        for pz in self.zones(self.pbag, self.pgen, h[3], self.phdr[i + 1][3], 41):
            inst = pz[41]
            ih, ih2 = self.inst[inst], self.inst[inst + 1]
            for iz in self.zones(self.ibag, self.igen, ih[1], ih2[1], 53):
                z = dict(GEN_DEFAULTS)
                z.update(iz)
                for oper, val in pz.items():
                    if oper in (41,) or oper in OFFSET_GENS:
                        continue
                    if oper in RANGE_GENS:
                        lo, hi = z.get(oper, (0, 127))
                        z[oper] = (max(lo, val[0]), min(hi, val[1]))
                    elif oper in (58, 54, 57):
                        continue
                    else:
                        z[oper] = z.get(oper, GEN_DEFAULTS.get(oper, 0)) + val
                kr, vr = z.get(43, (0, 127)), z.get(44, (0, 127))
                if kr[0] <= kr[1] and vr[0] <= vr[1]:
                    result.append(z)
        return result


# ---- signal processing ----

def resample(x, ratio):
    """Windowed-sinc resampling of x by ratio (output rate / input rate)."""
    n_out = int(math.floor(len(x) * ratio))
    if n_out <= 0:
        return np.zeros(0)
    cutoff = min(1.0, ratio) * 0.92
    half = 16
    t = np.arange(n_out) / ratio
    base = np.floor(t).astype(np.int64)
    frac = t - base
    acc = np.zeros(n_out)
    wsum = np.zeros(n_out)
    xp = np.concatenate([np.zeros(half * 2), x, np.zeros(half * 2)])
    for k in range(-half * int(math.ceil(1 / min(1.0, ratio))), half * int(math.ceil(1 / min(1.0, ratio))) + 1):
        d = k - frac
        w = cutoff * np.sinc(cutoff * d) * np.where(np.abs(d) < half / min(1.0, ratio),
                                                    0.5 + 0.5 * np.cos(np.pi * d * min(1.0, ratio) / half), 0)
        idx = base + k + half * 2
        ok = (idx >= 0) & (idx < len(xp))
        acc += np.where(ok, xp[np.clip(idx, 0, len(xp) - 1)], 0) * w
        wsum += w
    return acc


def timecents(tc):
    return 0.0 if tc <= -12000 else 2.0 ** (tc / 1200.0)


# ---- bank ----

class Zone:
    pass


def crossfade(a, b):
    w = np.linspace(0.0, 1.0, len(a), endpoint=False)
    return a * (1 - w) + b * w


def best_splice(y, ls, lo, hi, win):
    """Loop length in [lo, hi] whose end matches the material before ls."""
    ref = y[ls - win:ls]
    best, best_err = hi, None
    for n in range(lo, hi + 1, 2):
        seg = y[ls + n - win:ls + n]
        if len(seg) < win:
            break
        err = float(np.sum((seg - ref) ** 2))
        if best_err is None or err < best_err:
            best, best_err = n, err
    return best


def shorten(y, ls, le, rate, max_head, max_loop):
    """Caps the part before the loop and the loop itself, crossfading the
    joins; returns the new data and loop start/length."""
    cap = int(max_head * rate) & ~1
    xf = max(8, int(0.01 * rate))
    if ls > cap + 2 * xf:
        head = y[:cap].copy()
        head[cap - xf:] = crossfade(head[cap - xf:], y[ls - xf:ls])
        y = np.concatenate([head, y[ls:]])
        le -= ls - cap
        ls = cap
    maxl = int(max_loop * rate) & ~1
    if le - ls > maxl and ls >= xf:
        win = min(xf, ls)
        n = best_splice(y, ls, maxl // 2 & ~1, maxl, win)
        y = y[:ls + n].copy()
        y[ls + n - win:ls + n] = crossfade(y[ls + n - win:ls + n], y[ls - win:ls])
        le = ls + n
    return y, ls, le


def build_zone(sf, z, key_lo, key_hi, max_rate, max_len, used_hi, args):
    s = sf.shdr[z[53]]
    start = s[1] + z.get(0, 0) + 32768 * z.get(4, 0)
    end = s[2] + z.get(1, 0) + 32768 * z.get(12, 0)
    lstart = s[3] + z.get(2, 0) + 32768 * z.get(45, 0)
    lend = s[4] + z.get(3, 0) + 32768 * z.get(50, 0)
    looped = z.get(54, 0) in (1, 3) and lend - lstart >= 8
    root = z[58] if z.get(58, -1) >= 0 else s[6]
    tune = z.get(51, 0) * 100 + z.get(52, 0) + s[7]	# cents
    sr = s[5]
    # highest used note (with 2 semitones for bends) must stay within Paula's rate
    top = used_hi + 2
    rate = min(sr, max_rate, PAULA_MAX_RATE * 2.0 ** ((root - top - tune / 100.0) / 12.0))
    ratio = rate / sr
    x = sf.smpl[start:end].astype(np.float64)
    if looped:
        loop_len = (lend - lstart) * ratio
        n2 = max(2, int(round(loop_len / 2)) * 2)
        ratio *= n2 / loop_len
        rate = sr * ratio
        y = resample(x[:lend - start + 64], ratio)
        ls = int(round((lstart - start) * ratio)) & ~1
        y = y[:ls + n2]
        if len(y) < ls + n2:
            y = np.concatenate([y, np.zeros(ls + n2 - len(y))])
        # a note that dies away is heard in its first second or so, before the
        # loop; one that holds is heard in its loop
        if z.get(37, 0) >= 900:
            y, ls, le = shorten(y, ls, ls + n2, rate, args.max_ring, DYING_LOOP)
        else:
            y, ls, le = shorten(y, ls, ls + n2, rate, args.max_attack, args.max_loop)
        length, loop_start, loop_length = le, ls, le - ls
    else:
        limit = int(max_len * sr)
        if len(x) > limit:
            x = x[:limit].copy()
            fade = min(len(x), int(0.25 * len(x)))
            x[-fade:] *= np.linspace(1, 0, fade)
        y = resample(x, ratio)
        if len(y) & 1:
            y = np.concatenate([y, [0]])
        length, loop_start, loop_length = len(y), 0, 0
    peak = max(1.0, float(np.max(np.abs(y))) if len(y) else 1.0)
    q = np.clip(np.round(y * 127.0 / peak), -128, 127).astype(np.int8)
    zz = Zone()
    zz.key_hi, zz.root, zz.excl, zz.looped = key_hi, root, z.get(57, 0), looped
    zz.tune = int(round(tune * 64 / 100.0))
    # SF2 initialAttenuation scaled by 0.4 as FluidSynth does; the samples are normalised
    zz.atten = int(round(z.get(48, 0) * 0.4 + 200 * math.log10(32767.0 / peak)))
    zz.rate = rate
    zz.data = q.tobytes()
    zz.length, zz.loop_start, zz.loop_length = length, loop_start, loop_length
    zz.attack = int(round(timecents(z.get(34)) / TICK))
    zz.hold = int(round(timecents(z.get(35)) / TICK))
    zz.decay = int(round(timecents(z.get(36)) / TICK))
    zz.release = int(round(timecents(z.get(38)) / TICK))
    zz.sustain = max(0, min(1000, z.get(37, 0)))
    zz.key_lo = key_lo
    return zz


def candidates(sf, bank, program, vel=100):
    out = []
    for z in sf.preset_zones(bank, program):
        vr = z.get(44, (0, 127))
        if not vr[0] <= vel <= vr[1]:
            continue
        stype = sf.shdr[z[53]][9]
        if stype & 0x8000:
            continue
        out.append(z)
    # one sample per key range: mono or left of a stereo pair
    best = {}
    for z in out:
        kr = z.get(43, (0, 127))
        stype = sf.shdr[z[53]][9]
        rank = {1: 0, 4: 1, 2: 2}.get(stype & 7, 3)
        if kr not in best or rank < best[kr][0]:
            best[kr] = (rank, z)
    return sorted((v[1] for v in best.values()), key=lambda z: z.get(43, (0, 127)))


def melodic_zones(sf, program, lo, hi, args):
    """Up to --zones of the program's samples, spread over the used range;
    each takes the keys nearest its root."""
    cands = candidates(sf, 0, program)
    if not cands:
        return []
    kr = lambda z: z.get(43, (0, 127))
    covering = [z for z in cands if kr(z)[1] >= lo and kr(z)[0] <= hi]
    if not covering:
        covering = [min(cands, key=lambda z: abs(sum(kr(z)) / 2 - (lo + hi) / 2))]
    for k in range(1, min(args.zones, len(covering)) + 1):
        out = spread_zones(sf, covering, k, lo, hi, args)
        if min(z.rate for z in out) >= args.min_rate:
            break
    return out


def spread_zones(sf, covering, k, lo, hi, args):
    kr = lambda z: z.get(43, (0, 127))
    if len(covering) > k:
        if k == 1:
            covering = [covering[len(covering) // 2]]
        else:
            covering = [covering[round(i * (len(covering) - 1) / (k - 1))] for i in range(k)]
    roots = [z[58] if z.get(58, -1) >= 0 else sf.shdr[z[53]][6] for z in covering]
    out = []
    for i, z in enumerate(covering):
        key_lo = 0 if i == 0 else out[-1].key_hi + 1
        key_hi = 127 if i == len(covering) - 1 else max(key_lo, min(126, max(kr(z)[1], (roots[i] + roots[i + 1]) // 2)))
        if i < len(covering) - 1 and key_hi >= kr(covering[i + 1])[0] and kr(z)[1] < kr(covering[i + 1])[0]:
            key_hi = kr(z)[1]
        used_hi = max(min(hi, key_hi), key_lo)
        out.append(build_zone(sf, z, key_lo, key_hi, args.max_rate, args.max_len, used_hi, args))
    return out


def drum_zone(sf, key, args):
    for z in candidates(sf, 128, 0):
        kr = z.get(43, (0, 127))
        if kr[0] <= key <= kr[1]:
            return build_zone(sf, z, key, key, args.drum_rate, args.drum_len, key, args)
    return None


def write_bank(path, programs, drums, per_sound, nsounds):
    # a unit is one sample, so programs that share samples load them once
    zones, first, count, drum_index = [], [0xFF] * 128, [0] * 128, [0xFF] * 128
    blocks, unit_of_data, units_of = [], {}, {}
    for kind, n, zs in [('p', prog, programs[prog]) for prog in sorted(programs)] + \
            [('d', key, [drums[key]]) for key in sorted(drums)]:
        if kind == 'p':
            first[n], count[n] = len(zones), len(zs)
        else:
            drum_index[n] = len(zones)
        us = units_of[(kind, n)] = set()
        for z in zs:
            u = unit_of_data.get(z.data)
            if u is None:
                u = unit_of_data[z.data] = len(blocks)
                blocks.append(z.data + bytes(len(z.data) & 1))
            us.add(u)
            zones.append((z, u, 0))
    lists, table = [], []
    for sound in range(nsounds):
        progs, keys = per_sound.get(sound, ((), ()))
        us = sorted(set().union(*[units_of.get(('p', q), ()) for q in progs],
                                *[units_of.get(('d', k), ()) for k in keys]))
        table.append((len(lists), len(us)))
        lists += us
    head = 12 + 384
    unit_tab = head
    sound_tab = unit_tab + 8 * len(blocks)
    zone_tab = sound_tab + 4 * nsounds
    list_tab = zone_tab + 40 * len(zones)
    data_start = list_tab + 2 * len(lists)
    data_start += -data_start & 3
    out = bytearray(b'S5BK' + struct.pack('>HHHH', 2, len(zones), len(blocks), nsounds))
    out += bytes(first) + bytes(count) + bytes(drum_index)
    off = data_start
    for blk in blocks:
        out += struct.pack('>II', off, len(blk))
        off += len(blk) + (-len(blk) & 3)
    for f, n in table:
        out += struct.pack('>HH', f, n)
    for z, u, zoff in zones:
        out += struct.pack('>BBBBhhIIIIIHHHHHH', z.key_hi, z.root, z.excl, 1 if z.looped else 0, z.tune,
                           max(-32768, min(32767, z.atten)), int(round(z.rate * 256)), zoff, z.length,
                           z.loop_start, z.loop_length, min(z.attack, 65535), min(z.hold, 65535),
                           min(z.decay, 65535), min(z.release, 65535), z.sustain, u)
    for u in lists:
        out += struct.pack('>H', u)
    out += bytes(data_start - len(out))
    for blk in blocks:
        out += blk + bytes(-len(blk) & 3)
    open(path, 'wb').write(out)
    sizes = [sum(len(blocks[u]) for u in lists[f:f + n]) for f, n in table]
    return len(out), sum(len(b) for b in blocks), sizes


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('sf2', help='General MIDI SoundFont (e.g. FluidR3_GM.sf2)')
    ap.add_argument('index', help='index file (e.g. MI2DEMO.000)')
    ap.add_argument('data', nargs='+', help='data files (e.g. MI2DEMO.001 MI2DEMO.002)')
    ap.add_argument('-o', '--output', required=True, help='output bank')
    ap.add_argument('--max-rate', type=float, default=12000, help='melodic sample rate cap (Hz)')
    ap.add_argument('--min-rate', type=float, default=6000,
                    help='use another zone (up to --zones) rather than resample below this (Hz)')
    ap.add_argument('--drum-rate', type=float, default=12000, help='drum sample rate cap (Hz)')
    ap.add_argument('--drum-len', type=float, default=0.4, help='drum length cap (s)')
    ap.add_argument('--zones', type=int, default=3, help='most zones per melodic program')
    ap.add_argument('--max-len', type=float, default=0.8, help='unlooped melodic length cap (s)')
    ap.add_argument('--max-attack', type=float, default=0.15, help='cap on the part before a loop (s)')
    ap.add_argument('--max-loop', type=float, default=1.0, help='loop length cap (s)')
    ap.add_argument('--max-ring', type=float, default=1.5,
                    help='cap on the part before a loop for notes that die away (s)')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    melodic, drum_keys, per_sound, nsounds = scan_music(args.index, args.data)
    sf = SoundFont(args.sf2)
    programs, drums = {}, {}
    for prog in sorted(melodic):
        lo, hi = melodic[prog]
        zs = melodic_zones(sf, prog, lo, hi, args)
        if zs:
            programs[prog] = zs
        if args.verbose:
            for z in zs:
                print('program %3d keys %3d-%3d (used %d-%d) root %3d rate %5.0f len %6d loop %6d+%-6d atten %4d env %d/%d/%d/%d sus %d'
                      % (prog, z.key_lo, z.key_hi, lo, hi, z.root, z.rate, z.length, z.loop_start, z.loop_length,
                         z.atten, z.attack, z.hold, z.decay, z.release, z.sustain))
    for key in sorted(drum_keys):
        z = drum_zone(sf, key, args)
        if z:
            drums[key] = z
            if args.verbose:
                print('drum %3d root %3d rate %5.0f len %6d atten %4d env %d/%d/%d/%d sus %d excl %d'
                      % (key, z.root, z.rate, z.length, z.atten, z.attack, z.hold, z.decay, z.release, z.sustain, z.excl))
    total, samples, sizes = write_bank(args.output, programs, drums, per_sound, nsounds)
    used = sorted((n, i) for i, n in enumerate(sizes) if n)
    print('%s: %d bytes (%d of samples), %d programs, %d drum notes'
          % (args.output, total, samples, len(programs), len(drums)))
    if used:
        print('per song: median %d, largest %s' % (used[len(used) // 2][0],
              ', '.join('%d (sound %d)' % u for u in used[:-6:-1])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
