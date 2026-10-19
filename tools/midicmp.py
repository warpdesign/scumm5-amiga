#!/usr/bin/env python3
"""Compare the MIDI stream of scummrun -m with a ScummVM --dump-midi log.

  midicmp.py REFERENCE.txt OURS.txt

The reference holds 'MIDI : delta(...), message(0x.. 0x.. [0x..])' lines
(scripts/scummvm-midi.sh); ours holds 'MS STATUS P1 P2' lines in hex.
Messages are matched in order within a small window; times are reported,
not compared, since ScummVM runs in real time.
"""
import re
import sys


def load_ref(path):
    out, t = [], 0
    for line in open(path, errors='replace'):
        if not line.startswith('MIDI :'):
            continue
        d = re.search(r'delta\(([^)]*)\)', line).group(1).split(',')
        v = 0
        for b in d:
            v = v << 7 | (int(b, 16) & 0x7F)
        t += v
        m = [int(x, 16) for x in re.search(r'message\(([^)]*)\)', line).group(1).split()]
        if len(m) == 2:
            m.append(0)
        out.append((t, tuple(m)))
    return out


def load_ours(path):
    out = []
    for line in open(path):
        f = line.split()
        out.append((int(f[0]), tuple(int(x, 16) for x in f[1:4])))
    return out


def fmt(m):
    return ' '.join('%02x' % b for b in (m if (m[0] & 0xF0) not in (0xC0, 0xD0) else m[:2]))


def main():
    ref, ours = load_ref(sys.argv[1]), load_ours(sys.argv[2])
    window = int(sys.argv[3]) if len(sys.argv) > 3 else 48
    # ScummVM runs its 100 Hz timer in real time, so messages from different
    # sources (script commands, two songs) can swap within a few ticks:
    # match each reference message to the same message among the next
    # few unmatched ones of ours.
    r0, o0 = ref[0][0], ours[0][0]
    used = [False] * len(ours)
    start, missing, worst = 0, [], 0
    n = len(ref)
    for i in range(n):
        while start < len(ours) and used[start]:
            start += 1
        for j in range(start, min(len(ours), start + window)):
            if not used[j] and ours[j][1] == ref[i][1]:
                used[j] = True
                worst = max(worst, abs((ref[i][0] - r0) - (ours[j][0] - o0)))
                break
        else:
            if start >= len(ours):
                n = i
                break
            missing.append(i)
            if len(missing) >= 20:
                n = i + 1
                break
    extra = [j for j in range(start, min(len(ours), start + window)) if not used[j]] if missing else []
    print('%d reference messages checked (ref has %d, ours %d), %d without a match'
          % (n, len(ref), len(ours), len(missing)))
    for i in missing[:10]:
        print('  ref #%d at %d ms: %s' % (i, ref[i][0] - r0, fmt(ref[i][1])))
    for j in extra[:10]:
        print('  ours unmatched #%d at %d ms: %s' % (j, ours[j][0] - o0, fmt(ours[j][1])))
    print('largest time difference %d ms' % worst)
    return 1 if missing else 0


if __name__ == '__main__':
    sys.exit(main())
