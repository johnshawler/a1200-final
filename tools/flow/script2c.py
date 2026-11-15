#!/usr/bin/env python3
"""script2c.py in.txt out.h - a tools/nesref input script as a C table for a debug build's bench input
(CFLAGS=-DBENCH_SCRIPT, out.h = src/bench_script.h), to run the same session on the Amiga as host_test INPUT=in.txt."""
import sys
B = {'RIGHT': 1, 'LEFT': 2, 'DOWN': 4, 'UP': 8, 'A': 32, 'B': 16, 'START': 64, 'SELECT': 128}   # src/logic.h
rows = []
for l in open(sys.argv[1]):
    p = l.split()
    if len(p) < 2: continue
    bits = 0
    for t in p[2:]: bits |= B.get(t, 0)
    rows.append('    { %s, %s, %d },' % (p[0], p[1], bits))
open(sys.argv[2], 'w').write('static const unsigned bench_script[][3] = {\n' + '\n'.join(rows or ['    { 0, 0, 0 },']) + '\n};\n')
