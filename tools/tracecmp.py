#!/usr/bin/env python3
# scumm5-amiga - compare two SCUMM opcode traces in ScummVM's debug format
# SPDX-License-Identifier: GPL-3.0-or-later
"""Report the first divergence between a ScummVM trace and ours.

Only "Script ..." and "runScript(...)" lines are compared; other lines
(ScummVM log output) are ignored.
"""
import sys


def lines(path):
    with open(path, errors='replace') as f:
        return [l.rstrip('\n') for l in f if l.startswith(('Script ', 'runScript('))]


def main():
    if len(sys.argv) < 3:
        print('usage: tracecmp.py REFERENCE OURS [CONTEXT]')
        return 2
    ref, ours = lines(sys.argv[1]), lines(sys.argv[2])
    ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    n = min(len(ref), len(ours))
    for i in range(n):
        if ref[i] != ours[i]:
            print('first difference at line %d (of %d reference, %d ours)' % (i + 1, len(ref), len(ours)))
            for j in range(max(0, i - ctx), min(n, i + ctx)):
                mark = '  ' if ref[j] == ours[j] else '!!'
                print('%s %-60s | %s' % (mark, ref[j][:60], ours[j][:60]))
            return 1
    print('identical for %d lines (reference %d, ours %d)' % (n, len(ref), len(ours)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
