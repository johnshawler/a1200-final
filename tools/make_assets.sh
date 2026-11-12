#!/bin/bash
# Usage: tools/make_assets.sh
# Makes everything the build needs from the original ROM (original/$GAME.nes, GAME in port.conf), with no manual step:
#   assets/prg.bin, assets/chr.bin   the program and graphics ROMs, split from the iNES file
#   src/gen_*.c, src/gen_code.h      the game's 6502 code translated to C by tools/recomp.py
# The flow files in tools/flow/ (entry points seen while tracing the game with tools/nesref) seed the translation.
set -e
cd "$(dirname "$0")/.."
. ./port.conf
ROM=original/$GAME.nes
[ -f "$ROM" ] || { echo "make_assets: put the game's ROM at $ROM (it is not in the repository)"; exit 1; }
mkdir -p assets
python3 -I - "$ROM" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
assert d[:4] == b'NES\x1a', 'not an iNES file'
mapper = (d[6] >> 4) | (d[7] & 0xf0)
assert d[4] == 8 and d[5] == 16 and mapper == 1, ('only 128K PRG + 128K CHR MMC1 games are supported so far '
    '(this ROM: %d x 16K PRG, %d x 8K CHR, mapper %d): see PLAYBOOK-NES.md section 8' % (d[4], d[5], mapper))
open('assets/prg.bin', 'wb').write(d[16:16 + 0x20000])
open('assets/chr.bin', 'wb').write(d[16 + 0x20000:16 + 0x40000])
PY
FLOWS=$(ls tools/flow/*.txt 2>/dev/null | grep -v "_input.txt" || true)    # traces from tools/nesref -c; none is fine to start
python3 tools/recomp.py --game "$GAME" "$ROM" src $FLOWS
