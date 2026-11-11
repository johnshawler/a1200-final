# The game to port

**Little League Baseball: Championship Series** (SNK, 1990) for the NES/Famicom, USA release.

- What is here: nothing in the repository. The ROM is copyrighted and this repository is public, so it is not
  committed (`.gitignore` keeps it out, and everything derived from it: `assets/`, `src/gen_*`, `src/gen68k.s`).
- To build, put the iNES file here as `original/llb.nes`
  (`Little League Baseball - Championship Series (USA).nes`, 262,160 bytes, SHA-1
  `45564d865b65fabea7687b3e6fe6512c1129a0ab`), then run `tools/make_assets.sh`.
- Hardware: 128 KB PRG ROM + 128 KB CHR ROM on an MMC1 board (mapper 1), 6502 at 1.79 MHz, NTSC, **60 Hz**.
- Controls on the NES: pad, A, B, Select, Start. Modes: Championship series, Exhibition (1P, 2P, watch game),
  power analysis. "Watch game" plays computer against computer: the bench uses it.
- Running it without a window: `tools/nesref/nesref.c` is a reference NES built for this port. It runs the ROM
  headless with scripted input and dumps frames:

      cc -O2 tools/nesref/nesref.c -o nesref
      ./nesref original/llb.nes 3000 -i tools/flow/watch_input.txt -d 1000,2000 -o /tmp

  Its `-c` option writes the traces in `tools/flow/` that seed the translation.
