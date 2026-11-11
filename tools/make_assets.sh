#!/bin/bash
# Usage: tools/make_assets.sh
# Makes everything the build needs from the original ROM (original/llb.nes), with no manual step:
#   assets/prg.bin, assets/chr.bin   the program and graphics ROMs, split from the iNES file
#   src/gen_*.c, src/gen_code.h      the game's 6502 code translated to C by tools/recomp.py
# The flow files in tools/flow/ (entry points seen while tracing the game with tools/nesref) seed the translation.
set -e
cd "$(dirname "$0")/.."
ROM=original/llb.nes
[ -f "$ROM" ] || { echo "make_assets: put the game's ROM at $ROM (it is not in the repository)"; exit 1; }
mkdir -p assets
python3 -I - "$ROM" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'NES\x1a' and d[4] == 8 and d[5] == 16 and ((d[6] >> 4) | (d[7] & 0xf0)) == 1, 'expected the 128K+128K MMC1 ROM'
open('assets/prg.bin', 'wb').write(d[16:16 + 0x20000])
open('assets/chr.bin', 'wb').write(d[16 + 0x20000:16 + 0x40000])
PY
python3 tools/recomp.py "$ROM" src tools/flow/attract.txt tools/flow/watch.txt tools/flow/play1p.txt
