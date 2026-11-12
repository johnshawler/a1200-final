#!/usr/bin/env python3
"""mk1p.py frames seed [mode] - input script (tools/nesref format) for long random play: exhibition 1P (mode 1p)
or 2P (mode 2p), then random buttons, with A and START often (to get past screens). For coverage traces."""
import random, sys
n, seed = int(sys.argv[1]), int(sys.argv[2]); mode = sys.argv[3] if len(sys.argv) > 3 else '1p'
r = random.Random(seed)
out = ['100 105 START', '200 205 DOWN', '250 255 A']
if mode == '2p': out += ['330 335 DOWN']
out += ['400 405 A']
f = 500
while f < n:
    d = r.randint(2, 30)
    b = r.choice(['A', 'A', 'A', 'B', 'START', 'UP', 'DOWN', 'LEFT', 'RIGHT', 'A UP', 'A DOWN', 'A LEFT', 'A RIGHT',
                  'B LEFT', 'B RIGHT', 'B UP', 'B DOWN', 'SELECT', '', ''])
    if b: out.append('%d %d %s' % (f, f + d, b))
    f += d + r.randint(1, 40)
print('\n'.join(out))
