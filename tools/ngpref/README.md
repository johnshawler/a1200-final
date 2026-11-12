# tools/ngpref: the reference Neo Geo Pocket Color, and how it was checked

`ngpref.c` is a host-only model of the console, used to look at a game, measure it and trace it. It never runs
on the Amiga. Build and options:

    cc -O2 -o ngpref tools/ngpref/ngpref.c src/ngpc/k2ge.c
    ./ngpref rom.ngc FRAMES [-b bios.ngp] [-i input.txt] [-d f1,f2,..] [-o dir] [-s] [-p f1,f2,..]
             [-w a1,a2,..] [-W a1,a2,..] [-c flow.txt] [-r ramfile] [-z z80file] [-t]

| Option | What it gives |
| --- | --- |
| `-b` | the BIOS image; only its character set is used (system call 5). Without it that text shows as boxes |
| `-i` | scripted input: lines `first last BUTTONS` with UP DOWN LEFT RIGHT A B OPTION, frames inclusive |
| `-d`, `-o` | pictures of these frames as `frame_NNNNN.ppm` |
| `-s` | per frame: main CPU instructions and busy states, interrupts; sound program instructions and interrupts |
| `-p` | for these frames, the 12 addresses where most instructions ran, for both processors: the wait loops |
| `-w`, `-W` | addresses of the main CPU's wait loop and of the sound program's idle loop: left out of `-s` |
| `-c` | the flow file that seeds `tools/recomp900.py` |
| `-r` | work RAM after every frame |
| `-z` | the sound program (the Z80's 4 KB) each time the game starts the Z80 |
| `-t` | every instruction with the registers |

At the end it prints the BIOS calls the game used and a `uses:` line: what the game touched that this model
does not have (sound output, micro DMA, serial link, flash writes, mono mode).

## What was checked, and how

1. **Opcode tables against MAME.** `python3 tools/ngpref/check_tables.py <MAME>/src/devices/cpu/tlcs900/dasm900.cpp`
   compares the tables of `tools/tlcs900.py` with the tables of MAME's TLCS-900 disassembler, entry by entry:
   3,725 entries, 0 differ (MAME 0.264; the two instructions MAME lists that exist on the older TLCS-900 only
   are named in the script). MAME's file is read as text; nothing of it is run or copied.
2. **Decoder against assembler.** `python3 tools/ngpref/test_asm.py [files]` prints every byte sequence that
   decodes (random bytes, and every offset of the files given), assembles the text again and decodes once more:
   600,458 instructions in 913 forms, 0 failures (random bytes, the BIOS image and the test game).
3. **Cycle counts.** `src/ngpc/cycles900.h` holds the base counts of MAME's TLCS-900/H core (BSD-3-Clause,
   Wilbert Pol), read from its tables by a script; the additions for addressing modes, taken branches and
   shifts in `cpu900.h` follow the same core. They are the data sheet's numbers as far as MAME has them.
4. **The whole machine against Mednafen.** Neo Turf Masters was run in the Mednafen 1.29 packaged by Ubuntu
   (its own BIOS replacement, under a virtual display), the screen grabbed, and the 160 x 152 picture compared
   with this program's frame dumps: at two moments of the intro, 0 of 24,320 pixels differ. The game reaches
   those pictures only after its sound program has answered it several times, so the comparison covers the CPU,
   the Z80, the timers and the picture, for the instructions and features this one game uses up to there.
5. **The game itself.** It plays from power-on through its intro, title, menus and a hole of golf, 3,600 frames,
   without meeting bytes that are not an instruction.

## What was not checked

- Nothing ran on a real console, and the real BIOS code is not executed: the start state and the system calls
  are the ones other emulators document.
- Instructions the test game does not use are covered by the table checks only, not by execution. Flags the
  data sheet leaves undefined follow one choice (`src/ngpc/cpu900.h`).
- A differential test of the interpreter against Mednafen's or MAME's CPU core, instruction by instruction,
  was planned and not run: it would mean compiling their source in this session.
- Timer rates follow MAME (prescaler, timer 3's flip-flop as the Z80's interrupt); Mednafen uses other rates
  for the same registers. Music tempo depends on it; game logic was the same either way as far as tested.
