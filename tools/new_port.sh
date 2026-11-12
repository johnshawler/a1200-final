#!/bin/bash
# Usage: tools/new_port.sh <game> <rom.nes>
# Turns a copy of this repository into the start of a new NES port, keeping everything earlier ports learned:
#   - this port's records (REPORT.txt, NOTES.md, LEARNINGS.md, PIPELINE.md, traces) move to history/<old game>/
#   - PIPELINE.md keeps its milestone order (part A) and gets an empty backlog; NOTES.md starts empty
#   - tools/games/<game>.py is created empty (tools/games/<old game>.py stays: it is the example to copy from)
#   - the bench input is reset, the ROM is copied to original/<game>.nes (never committed: .gitignore)
#   - port.conf names the new game; generated files are removed
# PLAYBOOK-NES.md, the translator, the run time, the display, the sound and the debug tools are kept as they are.
# Run it on a fresh copy (Use this template on GitHub, or a clone), then commit. Nothing is pushed.
set -e
cd "$(dirname "$0")/.."
GAME="$1"; ROM="$2"
[ -n "$GAME" ] && [ -f "$ROM" ] || { echo "usage: tools/new_port.sh <game> <rom.nes>"; exit 2; }
case "$GAME" in *[!a-z0-9_]*) echo "new_port: the game name must be lower-case letters, digits and _"; exit 2;; esac
OLD=$(sed -n 's/^GAME=\([a-z0-9_]*\).*/\1/p' port.conf)
[ -n "$OLD" ] || OLD=previous
[ "$OLD" != "$GAME" ] || { echo "new_port: port.conf already names $GAME"; exit 2; }
[ ! -e "history/$OLD" ] || { echo "new_port: history/$OLD exists already"; exit 2; }

echo "new_port: archiving $OLD to history/$OLD"
mkdir -p "history/$OLD"
for f in REPORT.txt NOTES.md LEARNINGS.md PIPELINE.md original/README.md; do
  [ -e "$f" ] && cp "$f" "history/$OLD/$(basename "$f")"
done
[ -e original/README.md ] && mv "history/$OLD/README.md" "history/$OLD/original-README.md"
[ -d tools/flow ] && cp -r tools/flow "history/$OLD/flow"
cp src/logic.c "history/$OLD/logic.c"                        # its bench input, for reference
git rm -q --cached REPORT.txt LEARNINGS.md 2>/dev/null || true
rm -f REPORT.txt LEARNINGS.md
find tools/flow -name '*.txt' -delete 2>/dev/null || true

python3 - "$GAME" "$OLD" <<'PY'
import sys, re
game, old = sys.argv[1], sys.argv[2]
# PIPELINE.md: keep part A, new empty backlog
s = open('PIPELINE.md').read()
a = s.index('## Part B')
open('PIPELINE.md', 'w').write(s[:a] + '''## Part B: backlog for this port

(Fill in after the load measurement and the first profile. The last port's backlog is in history/%s/PIPELINE.md.)
''' % old)
open('NOTES.md', 'w').write('# Notes: where the brief was unclear, wrong or missing something\n\n'
    '(Earlier ports: history/*/NOTES.md. Add one line for every new place.)\n')
# bench input: empty until the self-playing mode is found
for p in ('src/logic.c', 'tools/nesref/verify.c'):
    t = open(p).read()
    t = re.sub(r'(/\* BENCH INPUT BEGIN[^\n]*\n).*?(    /\* BENCH INPUT END \*/)',
               r'\1    (void)t;      /* TODO: script the self-playing mode through the busiest scenes (PLAYBOOK-NES.md section 2) */\n\2',
               t, flags=re.S)
    open(p, 'w').write(t)
# the game's knowledge file
open('tools/games/%s.py' % game, 'w').write('''"""%s.py - what the translator needs to know about this game that it cannot find out by itself.
Loaded by tools/recomp.py (--game %s). See tools/games/%s.py for a filled-in example and PLAYBOOK-NES.md
sections 3-5 for how to find each entry.
"""
NORETURN = set()        # subroutines that pop their own return address (jump-table dispatchers)
HLE = {}                # (bank, address): hand-written 68k text, exact, proven by the checksum
HLE_FALLBACK = set()    # entries of HLE whose text may branch to .orig
''' % (game, game, old))
# port.conf
c = open('port.conf').read()
c = re.sub(r'^GAME=\S+', 'GAME=%s' % game, c, flags=re.M)
c = re.sub(r'^NAME=\S+', 'NAME=%s' % game, c, flags=re.M)
open('port.conf', 'w').write(c)
open('original/README.md', 'w').write('''# The game to port

**(name, publisher, year)** for the NES. The ROM is `original/%s.nes`; it is not in the repository (copyrighted;
`.gitignore` keeps it and everything made from it out).

- Mapper, PRG/CHR sizes, mirroring, 50/60 Hz:
- Self-playing mode for the bench:
- Controls:
- Load (6502 instructions per frame outside wait loops, per scene):

The previous port's description is in history/%s/original-README.md.
''' % (game, old))
PY
cp "$ROM" "original/$GAME.nes"
rm -f src/gen_*.c src/gen_code.h src/gen_waits.txt src/gen68k.s assets/prg.bin assets/chr.bin
cat <<EOF
new_port: ready for $GAME. Next, as in PLAYBOOK-NES.md section 2:
  1. tools/setup.sh; read PLAYBOOK-NES.md and history/$OLD/LEARNINGS.md
  2. check the ROM fits (make_assets.sh says if the mapper or size is not supported yet)
  3. trace with tools/nesref (-c into tools/flow/), measure the load per scene, write the verdict
  4. tools/make_assets.sh, host test, tools/nesref/verify.c, then the Amiga checksum, then display and speed
  5. at the end: REPORT.txt, a new LEARNINGS.md, and a lessons-log entry in PLAYBOOK-NES.md
EOF
