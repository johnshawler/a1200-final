# Amiga 1200 Game Porting Brief


A work brief for a coding model: port any game to a stock Amiga 1200 and hold full speed, by keeping the game's logic, rebuilding its display on what the AGA chipset does for free, and budgeting every frame before writing code.

## How to use this brief

You are a coding model porting one game to the Commodore Amiga 1200. Do Steps 1 to 4 in order, copy the kit code unchanged, and measure before you claim a result.

**Target.** A stock A1200: 68EC020 at 14 MHz, 2 MB chip RAM, no fast RAM, AGA chipset, Kickstart 3.0 or 3.1, PAL at 50 Hz. Anything faster is a bonus, never an assumption.

**What 100% speed means.** Two things, both measured:

1. Game time is right. Logic advances at the original rate by the clock, even when drawing is slow. No slow motion. Only below 6 pictures a second does the skeleton let game time fall behind.
2. The display is full rate. A new picture at every vertical blank, 50 a second, with no frame over budget.

When the second is physically out of reach on a stock machine (a software 3D renderer is the usual case), deliver the first, report the frame rate you measured, and name the cheapest upgrade that reaches 50.

**The one idea behind every rule.** A PC game clears the screen and redraws everything each frame. A stock A1200 cannot. Clearing its 256-colour screen once costs 70% of a frame. Copying one costs 1.4 to 1.8 frames. A full-speed port never repaints: it lets the chipset move pixels (hardware scroll, sprites, colour changes) and touches only what changed.

**Never do these.** Each one has turned a port into a slideshow:

- Compile the original renderer, or an SDL layer, and convert its framebuffer every frame.
- Call operating-system drawing functions in the game loop.
- Use `float` or `double`. There is no FPU.
- Write your own startup, interrupt or display set-up code. Copy the kit.
- Emulate another CPU, or interpret a script, at run time.
- Report a frame rate you did not measure in a cycle-exact emulator or on a real machine.

## The machine and its frame budget

One PAL frame is 71,051 bus slots, and every memory access by the CPU, the blitter or the display spends them. Budget in slots, not milliseconds.

| Part | Stock A1200 | What it means for a port |
| --- | --- | --- |
| CPU | 68EC020 at 14.19 MHz. No FPU, no MMU, 24-bit addresses | No floating point. Never hide flags in the top byte of a pointer |
| Cache | 256-byte instruction cache, no data cache | An inner loop under 256 bytes, callees included, runs without fetching code |
| Memory | 2 MB chip RAM and nothing else. About 1.7 MB free when a game starts | Code, data and screen share one bus with the video chips. Plan for 1.5 MB |
| Screen | 320 x 256 at 50 Hz, up to 8 bitplanes: 256 colours from 16.7 million | Pixels are planar: 8 separate 1-bit images, not one byte per pixel |
| Frame | 313 lines x 227 slots = 71,051 slots = 20.03 ms (49.92 Hz) | This is the whole budget. One slot is 282 ns |
| CPU memory access | 2 slots each, whatever the size | A byte costs the same as a 32-bit long. Use longs at addresses divisible by 4 |
| Blitter | 2 slots per 16-bit word to clear or copy, 4 to draw through a mask | The same blitter as a 1985 Amiga. It is not faster than this CPU |
| Sprites | 8, each 16, 32 or 64 pixels wide, any height. 3 colours, or 15 as a pair | Moving objects that cost no drawing time |
| Sound | 4 channels of 8-bit samples, played by hardware | Costs almost no CPU |
| NTSC machine | 263 lines, about 59,800 slots, 59.8 Hz | 16% less time per frame. The skeleton switches such a machine to 50 Hz while the game runs |

### What things cost

Measured on an emulated stock A1200 running the kit's own calls. Read the last column as: how much of one 50 Hz frame is gone. Step 2 has the full table by plane count.

| Operation | Slots | Share of a frame |
| --- | --- | --- |
| Move a hardware sprite | 160 | 0.2% |
| Draw one 16 x 16 background tile, 6 planes | 1,400 | 2.0% |
| Draw and erase a 16 x 16 object, 6 planes | 2,430 | 3.4% |
| Draw and erase a 32 x 32 object, 6 planes | 5,690 | 8.0% |
| Draw and erase a 32 x 32 object, 8 planes | 8,560 | 12.0% |
| Draw and erase a 64 x 64 object, 6 planes | 16,690 | 23.5% |
| Clear the whole 8-plane screen (CPU, the fastest way) | 49,800 | 70% |
| Copy the whole 8-plane screen (blitter, the fastest way) | 98,400 | 138% |
| Convert a 160 x 128 byte-per-pixel buffer, shown double size | 124,500 | 175% |
| Convert a 320 x 256 byte-per-pixel buffer | 458,000 | 645% |
| The same conversion written as plain C loops (320 x 200) | 5,300,000 | 75 frames |

What is left for game logic: about 16,000 to 26,000 instructions of ordinary compiled code per frame (real-machine benchmark figures: 0.8 to 1.3 million a second). A tight loop that stays in the cache and in registers runs four times faster than that. The machine is not slow at computing. It is slow at touching memory.

### Faster machines

| Upgrade | Gain | What does not change |
| --- | --- | --- |
| Trapdoor fast RAM, same CPU | About 1.5 times on average (1.2 to 2.2). Code and data there ignore the display | Writing to the screen. The 320 x 256 conversion drops only from 6.5 to 5.4 frames |
| 68030 at 50 MHz with fast RAM | About 6 times for logic. The 320 x 256 conversion takes about 1.4 frames | Chip RAM writes stay near 7 MB a second on every accelerator |

## Speed rules

Twelve rules, most important first. Each number was measured or comes from the chip manuals.

1. **Never repaint.** Draw the background once. Each frame after that, the kit erases and redraws the moving objects and nothing else is touched. A full clear alone is 70% of a frame.
2. **Let the hardware move things.** Scrolling is two register values per frame. A sprite move is 0.2% of a frame. Put the player, bullets, cursors and score digits on sprites first.
3. **Use the fewest bitplanes the art needs.** Blitter work grows with every plane. A 32 x 32 object costs 5.7% of a frame in 4 planes, 8.0% in 6 and 12.0% in 8. Six planes give 64 colours and the sprites bring 16 more.
4. **Set the fetch mode.** With more than 4 planes, FMODE bits 1-0 must be 3 (or 1 when scrolling with sprites). Left at 0, an 8-plane screen takes 160 of the 227 slots on every line and the CPU gets half as many memory accesses per frame: 15,100 instead of 29,900. The kit sets it. Do not write your own display set-up.
5. **Work in 32-bit longs at addresses divisible by 4.** One memory access costs 2 slots whether it moves 8 bits or 32. A byte loop is four times slower than a long loop over the same data.
6. **No float, no divide, few multiplies in loops.** Use fixed point (24.8 or 16.16 in an `int`). A 16-bit multiply is 27 CPU clocks, a 32-bit divide 90, as much as eleven long writes. Replace them with tables built before the game starts.
7. **Keep inner loops small and in registers.** A loop under 256 bytes runs from the cache. Local variables in registers cost nothing. Every global or array access is 2 slots.
8. **Tie game time to the vertical blank.** Logic runs in fixed ticks counted from the frame counter, as the skeleton does. No delay loops, no timing by counting.
9. **Convert every asset on the host.** What the Amiga loads is already in its final form: planar, big-endian, masks made, palette reduced. Nothing is converted at run time.
10. **Load everything before takeover.** While the game runs there is no file access, no `AllocMem`, no `printf`. The operating system is switched off.
11. **Blitter for masked objects and tiles, CPU for clears and long copies.** The CPU clears twice as fast as the blitter. The blitter draws a masked object at any x position in about 60% of the time a CPU routine needs.
12. **Measure after every milestone.** Keep the worst frame under 80% of the budget. The emulator charges too little for multiply, divide and shift, so real hardware is slower for arithmetic-heavy code.

## Step 1: Classify the game and pick the route

Find the row that matches what you were given and how the game draws. The route in that row is the fastest one that reaches full speed.

| What you have | Route | Result on a stock A1200 |
| --- | --- | --- |
| C or C++ source. Picture mostly still: puzzle, board, cards, single-screen arcade | Keep the logic files. Delete the renderer. Draw the background once, moving things as sprites and kit objects | 50 Hz |
| C or C++ source. 2D scrolling: platformer, shooter, top-down | Keep the logic. Rebuild the picture as a hardware-scrolled tile map with sprites and kit objects | 50 Hz inside the object budget of Step 2 |
| C or C++ source. Per-pixel rendering: ray-caster, Doom-like, voxel, rotate-and-zoom, video | Keep the logic at 50 ticks a second. Render in fixed point into a byte buffer of 160 x 128 or smaller. Convert it with the kit's C2P routine | About 10 frames a second. 50 is out of reach: say so in the hand-back |
| Assembly or disassembly for a Z80, 6502 or 6809 machine | Translate the code once, on the host, to 68000 assembly or C. The repository github.com/jotd666/amiga68ktools has `z80268k.py`, `6502to68k.py` and `6809to68k.py`. Replace video, sound and input hardware with kit calls | 50 Hz |
| 68000 code for another machine: Atari ST, Mega Drive, arcade boards | Keep the game code. Replace every hardware access. Convert the graphics to planar | 50 Hz inside the object budget |
| Script or engine project: Unity, Godot, GameMaker, PICO-8, JavaScript, Python, Lua | Rewrite the logic in C from the scripts. The design is the source, not the code | 50 Hz for 2D games |
| Binary only | Ask the user before starting. Rebuilding from observed behaviour is the slowest route | Depends on the game |

Never run the original CPU or a script interpreter on the Amiga. An interpreted 68000 core reaches about 2.5 MHz on this machine.

### Split the source into two piles

- **Keep:** rules, physics, collision, enemy behaviour, level data, scoring, state machines. This pile must end up as plain C that includes no platform header.
- **Replace:** everything that touches pixels, sound devices, files, the clock or input. None of it survives.

The seam between the piles is three functions: one tick of logic, one draw, one read of input. The kit skeleton already has them.

### If the original runs at 60 Hz

Pick one:

- **Exact and quick:** set `TICKS_PER_SECOND` to 60 in the skeleton. The loop then runs six logic ticks every five frames. Game speed is exact. Scrolling shows a small judder every fifth frame.
- **Smooth:** leave it at 50 and rescale the constants on the host. Speeds in pixels per tick: multiply by 6/5. Durations in ticks, such as cooldowns and timers: multiply by 5/6. Anything computed from a running tick count, such as an animation frame or a sine phase: use the count times 6/5 in place of the count. Use this route for scrolling games.

## Step 2: Choose the display and check the budget

Pick one row, copy its values into the kit's `DispCfg`, then add up the budget. Do this before writing any game code.

| The game needs | `planes` | `scroll` | `fetch` | `buffers` | Sprites that work |
| --- | --- | --- | --- | --- | --- |
| A still screen | fewest that hold the colours | 0 | 3 | 3 (1 if you never call `disp_bob`) | all 8 |
| Scrolling, up to 16 colours | 4 | 1 | 0 | 3 | 7 (numbers 0 to 6) |
| Scrolling, up to 64 colours, with sprites | 5 or 6 | 1 | 1 | 3 | 5 (numbers 0 to 4) |
| Scrolling, many blitter objects, no sprites | 5 to 8 | 1 | 3 | 3 | 1 (number 0) |
| Per-pixel rendering | 8 | 0 | 3 | 2, with `double_y` 1, `view_h` 256, `buf_h` 128 | all 8 |

The sprite counts are a hardware fact: scrolling makes the chipset fetch picture data earlier on each line, in the slots the higher-numbered sprites would use.

### Colours

- Planes to colours: 4 give 16, 5 give 32, 6 give 64, 8 give 256. Colour 0 is the background and is transparent in objects and sprites.
- Sprites take 16 colours starting at `sprite_bank` x 16. Set `sprite_bank` above the playfield's colours: 1 or more for 4 planes, 2 or more for 5 planes, 4 or more for 6 planes. With 8 planes the sprites share 16 of the playfield's colours.
- Inside that bank, sprites 0 and 1 use colours 1 to 3, sprites 2 and 3 use 5 to 7, sprites 4 and 5 use 9 to 11, sprites 6 and 7 use 13 to 15. An attached pair (two sprites on top of each other) uses all of 1 to 15.
- A PC palette is usually sparse. `tools/gfx.py` has `compact()` to renumber the colours a game really uses.

An attached pair uses all 15 colours of the bank, and the other sprites read the same bank. Choose the bank's colours so that entries 5 to 7, 9 to 11 and 13 to 15 also suit the single sprites that use them. `sprite_w` 32 and 64 work in every row of the table.

A status bar on a scrolling screen: one 64-pixel sprite per 64 pixels of bar, so choose the row with `fetch` 1. A full-width bar takes all five sprites: the player and shots are then `disp_bob` objects and belong in the budget. Change digits by writing into the sprite data, only when the value changes. All sprites share one width. The kit has no split screen.

Each sprite number needs its own copy of its data block, because `disp_sprite` stores the position inside it. Four bullets from one image are four copies, made with `sys_chip_copy`. A sprite taller than 18 lines cannot slide off the top edge: the kit switches it off instead.

With `buffers` 1 every drawing call writes straight into the visible picture. That suits small changes on a still screen. `disp_bob` needs `buffers` 3.

### What each kit call costs

Percent of one 50 Hz frame, measured. `disp_bob` includes erasing the object again.

| Kit call | 4 planes | 5 planes | 6 planes | 8 planes |
| --- | --- | --- | --- | --- |
| `disp_sprite`: move a sprite | 0.2 | 0.2 | 0.2 | 0.2 |
| `disp_tile`: one 16 x 16 tile into all buffers | 1.4 | 1.6 | 2.0 | 3.0 |
| `disp_paste`: a 32 x 16 area into all buffers | 1.8 | 2.2 | 2.6 | 3.5 |
| `disp_stamp`: a 16 x 16 piece into all buffers | 4.6 | 5.4 | 6.3 | 8.1 |
| `disp_bob`: 16 x 16 object | 2.5 | 2.8 | 3.4 | 4.9 |
| `disp_bob`: 32 x 32 object | 5.7 | 6.4 | 8.0 | 12.0 |
| `disp_bob`: 64 x 64 object | 16.8 | 18.5 | 23.5 | 34.7 |
| `disp_paste`: the whole 320 x 256 picture, once at start | 190 | 240 | 300 | 410 |

Painting pixels into the picture with your own CPU loops is not in this table and costs far more. A still screen that changes in parts, such as a brick wall, uses `disp_paste` to erase and `disp_stamp` to add.

### The budget check

1. List what is on screen in the game's busiest moment.
2. Move as much of it as you can to sprites.
3. Add the percentages of everything left, plus the tiles drawn in that frame, plus 15 for logic.
4. The total must be under 80. If it is not, cut in this order: fewer planes, smaller objects, fewer objects on screen at once, turning resting objects into stamps, or showing a new picture only every second vertical blank (25 pictures a second: say so in the hand-back).

Call `disp_bob` for every visible object in every frame, also when the object stands still: an object that is skipped vanishes from every second picture. One frame takes at most 48 `disp_bob` calls.

Worked example, measured: a 6-plane scrolling level with eight 32 x 32 objects, five sprites and four tiles a frame. The sum is 8 x 8.0 + 4 x 2.0 + 5 x 0.2 = 73. The measured run was 71% on average, 76% in the worst frame, and no late frame in 3,000. That demo has almost no logic. A real game with this load is over the limit once 15 is added for logic: use six such objects, or 16 x 16 ones.

### The chip RAM check

Display buffers take `buffers` x `planes` x (row x `buf_h` + `level_w` / 8 + 64) bytes. Row is 40 for a still screen and 64 for a scrolling one. Six planes, three buffers, 256 lines and a 4,096-pixel level come to 300 KB. Eight planes come to 400 KB. Buffers, all assets and the program together must stay under 1.5 MB. Keep every single file and every `data_c` section under 400 KB: memory must be free in one piece.

### Per-pixel games

The chunky route cannot reach 50 Hz on a stock machine. Converting 160 x 128 pixels takes 1.75 frames before a single pixel is rendered. A simple renderer in C on top of that gave 10 frames a second in the kit's demo, and a ray-caster ported with this brief gave 9.5, or 12.3 with fast RAM. Plan for about 10 at 160 x 128, keep the logic at 50 ticks, and shrink the view to gain speed. Draw the status bar, crosshair and weapon with sprites, never into the byte buffer.

## Step 3: Set up the toolchain and the kit

Everything below was run on Ubuntu 24.04. The kit is in this repository: `kit/`, `tools/` and `examples/`.

### Install

```bash
docker pull amigadev/crosstools:m68k-amigaos
```

```bash
apt-get install -y fs-uae xvfb imagemagick xdotool python3
```

The image holds m68k-amigaos-gcc 6.5.0b, the libnix C library, the Amiga NDK 3.2 headers and the vasm assembler. FS-UAE is the emulator used for measuring. It boots with a free replacement ROM, so no Commodore ROM is needed. Never download a Kickstart ROM: if a test needs the real one, ask the user for the file.

### Layout of a port

```text
mygame/
  kit/        sys.c sys.h disp.c disp.h perf.c perf.h   (copy unchanged)
  kit/c2p/    chunky-to-planar routines                   (per-pixel games only)
  kit/ptplayer/ music and sound-effect player
  tools/      build.sh bench.sh run_uae.sh gfx.py
  src/        main.c (start from kit/main.c) and the game's logic files
  assets/     converted data, made by your own script that calls tools/gfx.py
  dist/       what the user copies to the Amiga: the program and its data files
```

Copy `kit/main.c` to `src/main.c` and edit the copy. Put your own host scripts, such as the asset converter, in `tools/`. Put `game_tick`, `game_bench_input` and `game_checksum` in one logic file, `src/logic.c`, that includes no Amiga header: the host test and the Amiga build then compile the same code.

### Build and measure

```bash
CFLAGS="-Ikit" tools/build.sh dist/mygame kit/sys.c kit/disp.c kit/perf.c src/main.c src/logic.c src/data.s
```

```bash
tools/bench.sh dist mygame 1500 shot.png
```

`build.sh` compiles C with `-m68020 -O2 -fomit-frame-pointer -noixemul` and assembles `.s` and `.asm` files with `vasmm68k_mot -Fhunk -m68020`. `bench.sh` boots an emulated stock A1200, runs `mygame bench 1500`, saves a screenshot and prints the `bench.txt` the game wrote.

- Run both scripts from the project folder. Sources and the output must be inside it: nothing outside is visible to the compiler.
- Add include folders through `CFLAGS`, for example `CFLAGS="-Ikit -Isrc -Iassets"`.
- `incbin` paths in assembly files are looked up from the project folder: `incbin "assets/tiles.dat"`.
- Add `kit/ptplayer/ptplayer.asm` to the build line when the game has sound, and `kit/c2p/c2p2x1_8_c5_gen.s` for a per-pixel game.
- `bench.sh` takes the screenshot 20 seconds after the emulator starts. Set `SHOT_AT=40` in front of the command to take it later. Extra emulator options go after the screenshot name: `tools/bench.sh dist mygame 3000 shot.png --fast_memory=4096`.

In this repository two scripts wrap those steps. `tools/setup.sh` does the Install step. `tools/port.sh` reads `port.conf`, builds the port, runs the bench, runs the host test and checks the gates: it prints `PASS` or the gate that failed. `tools/port.sh fast` repeats the run with 4 MB of fast RAM. CI runs `tools/port.sh` on every push.

### The kit in one table

| Call | When | What it does |
| --- | --- | --- |
| `sys_alloc(size, memflags)` | before `sys_take` | Allocates memory that `sys_free_all` returns. `MEMF_CHIP` for graphics and samples |
| `sys_load(name, memflags, &size)` | before `sys_take` | Reads a whole file into such memory |
| `sys_chip_copy(data, size)` | before `sys_take` | Makes an own, aligned chip RAM copy of a block: one per sprite number |
| `disp_open(&cfg)` | before `sys_take` | Allocates the screen buffers and builds the display list |
| `sys_take()` | once everything is loaded | Switches AmigaOS off and starts the 50 Hz frame counter `sys_frame`. Drawing calls work from here on |
| `disp_palette`, `disp_on` | right after `sys_take` | Loads colours (0x00RRGGBB) and shows the screen |
| `disp_tile(x, y, tile)` | when background changes | Draws a 16 x 16 tile into every buffer |
| `disp_paste(picture, x, y, w, h)` | when background changes, still screens | Copies part of a full-screen picture into every buffer: draws the background, or erases a stamp |
| `disp_stamp(x, y, image, mask, wwords, h)` | when background changes | Draws a masked image into every buffer, to stay: a brick, a board piece |
| `disp_begin()` | start of each frame's drawing | Erases the objects drawn two frames ago |
| `disp_bob(x, y, image, mask, wwords, h)` | each frame, per object | Draws a masked object with the blitter, clipped at the screen edges |
| `disp_sprite(n, data, x, y, h, attach)` | each frame, per sprite | Positions a hardware sprite in screen pixels |
| `disp_scroll(x, y)` | each frame, if scrolling | Sets the world pixel shown at the top-left corner |
| `disp_flip()` | end of each drawn frame | Waits for the vertical blank and shows the new picture |
| `sys_joy(1)`, `sys_keys[]` | each frame | Joystick bits, key states by raw key code |
| `perf_frame_start`, `perf_frame_end`, `perf_save` | in the skeleton loop | Records frame times and writes `bench.txt` |
| `sys_give()`, `disp_close()`, `sys_free_all()` | on exit, in this order | Give the machine and all memory back |

Coordinates given to `disp_tile` and `disp_bob` are world pixels. Coordinates given to `disp_sprite` are screen pixels.

Use `sys_alloc` for every allocation and never `AllocMem`. AmigaOS does not free a program's memory when it ends: without `sys_free_all()` every run would lose its memory until the machine is rebooted.

### What this compiler does differently from a PC compiler

- `int`, `long` and pointers are 32 bits, `short` is 16, `char` is signed.
- The CPU is big-endian. A 16- or 32-bit value in a PC data file must be byte-swapped. Do it in the host converter.
- Structs are aligned to 2 bytes, not 4. `struct { char c; int i; }` is 6 bytes with `i` at offset 2. Never read a PC file by overlaying a struct on it. Never use `#pragma pack` tricks.
- Use the Amiga types `UBYTE`, `UWORD`, `ULONG`, `WORD`, `LONG` in Amiga-only files so sizes are explicit. The shared logic file uses plain C types, because the host has no Amiga headers.
- The stack is small: 4 KB when started from an icon or a plain Shell. Keep every array `static` or allocate it. Do not recurse deeply.
- `__chip` in front of a global puts it in chip RAM: `__chip UWORD image[] = { ... };`. In a vasm file use `section gfx,data_c` and `incbin`. Everything the blitter, the sprites, the display or the sound chip reads must be in chip RAM.
- A C function `foo` is `_foo` in assembly. Arguments can be put in registers: `void f(UBYTE *p __asm("a0"), ULONG n __asm("d0"));`. Assembly functions must preserve d2-d7 and a2-a6.

## Step 4: Build in this order

Ten milestones, each with a gate. Do not start a milestone until the gate before it passes. The order puts the cheap checks first, so a wrong plan fails in minutes, not after the game is written.

| # | Milestone | Gate |
| --- | --- | --- |
| 1 | Feasibility note: class and route (Step 1), `DispCfg`, colour count, every object with its size, the budget sum (Step 2), what will be cut | Budget sum under 80 |
| 2 | The kit skeleton builds and runs unchanged | `bench.sh` prints `late=0` |
| 3 | Logic pile compiles on the host as plain C: fixed point, no platform headers, one `game_tick(input)` function | A host test runs 3,000 ticks of scripted input and prints a checksum of the game state |
| 4 | Asset converter: one host script that writes the final Amiga data | No manual steps. Assets plus display buffers under 1.4 MB |
| 5 | Background on screen | Screenshot matches the original's background |
| 6 | Scrolling, if the game scrolls | `late=0` over the whole level in bench mode |
| 7 | Moving things: sprites first, then `disp_bob` objects | Screenshot shows them. `late=0`, `worst_pct` under 80 |
| 8 | Logic linked in, the real game plays | `bench.txt` shows the same checksum as the host test for the same number of ticks |
| 9 | Sound | Still `late=0` |
| 10 | Final bench of at least 3,000 ticks through the busiest scenes | `late=0`, `worst_pct` under 80. Then write the hand-back |

- **Milestone 3:** the host test is one small file, `src/host_test.c`. Its `main` calls `game_tick(game_bench_input(t))` for t from 0 to N-1 and prints `game_checksum()`. Build it with the host's own `cc` together with `src/logic.c`.
- **Milestone 8:** `bench N` stops at exactly N ticks, so run the host test with the same N.
- **Milestone 9:** if the original has no sound, write that in the hand-back and move on.
- **Per-pixel games:** `late=0` and `worst_pct` under 80 cannot be met and are not the gate. The gates are the equal checksum and a reported `shown_fps`. Milestone 1's budget is the conversion and render time from Step 2, not a sum under 80.

* **Types in the logic file:** use `int`, `short` and `char`, or the `<stdint.h>` types. Never `long`: it is 64 bits on the host and 32 on the Amiga, and the checksums will differ.
* **Sound from the logic file:** the logic must not call the sound player. Let `game_tick` set one bit per sound in a variable. Let `main.c` play and clear those bits after the tick loop.

### Converting the logic pile

Do these in order, on the host, re-running the checksum test after each:

1. Replace every `float` and `double` with fixed point. Positions and speeds become `int` in 24.8 format: multiply constants by 256, shift right by 8 to get pixels. `2.5f` becomes `640`.
2. Replace `sin`, `cos` and `sqrt` with tables of 256 or 1,024 entries built by the asset script.
3. Replace divisions inside loops with shifts or tables. Keep sizes at powers of two.
4. Remove every call to the old platform layer from the logic. Drawing happens in `game_draw`, nowhere else.
5. If the original used a variable time step (`dt`), make it fixed: one tick is exactly 1/50 second.
6. Replace `malloc` in the game loop with fixed arrays sized for the worst case.

### When a gate fails

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| `late` above 0 in a few frames only | A burst: a whole tile column, many objects spawning at once | Spread the work: a few tiles per frame, stagger spawns |
| `late` above 0 everywhere | Over budget by design | Go back to the budget check in Step 2 and cut |
| Checksum differs from the host | Byte order in data, `int` size, signed `char`, struct layout, or a float left in | Print the checksum every 100 ticks on both sides and find the first tick that differs |
| Black screen, or the machine hangs | An operating-system call after `sys_take`, or data the blitter reads is not in chip RAM | Move loading before `sys_take`. Use `__chip` or `MEMF_CHIP` |
| Garbage columns at the picture's edge | Objects drawn more than 64 pixels outside the view | Skip objects that are off screen |

## Reference code

Seven pieces, each copied from code that ran on the emulated stock A1200. The full files are in the kit folder.

### 1. The main loop

From `kit/main.c`. Replace the `game_*` functions, keep the loop.

```c
    if (!game_load() || !disp_open(&display)) { disp_close(); sys_free_all(); return 20; }
    sys_force_pal = 1;                                          /* NTSC machine: run the 256-line picture at 50 Hz */
    if (!sys_take()) { disp_close(); sys_free_all(); return 20; }
    sys_vbl_hook = sys_key_poll;                                /* keys are read at every vertical blank */
    hz = sys_is_pal ? 50 : 60;
    disp_palette(palette, 0, 48);
    disp_on();
    game_start();
    perf_reset();
    sys_wait_frame();
    last = sys_frame;
    for (;;) {
        t0 = sys_now();
        now = sys_frame; vblanks = now - last; last = now;      /* vertical blanks since the previous pass */
        if (frames == 0) vblanks = 1;
        perf_frame_start(vblanks);                              /* the report counts every vertical blank */
        if (vblanks > 8) vblanks = 8;                           /* game time: after a long stall, do not spiral */
        acc += vblanks * TICKS_PER_SECOND;                      /* game time follows the clock, not the renderer */
        while (acc >= hz && !(bench && ticks >= bench)) {       /* bench mode stops at exactly N ticks */
            acc -= hz; game_tick(bench ? game_bench_input(ticks) : sys_joy(1)); ticks++;
        }
        game_draw();
        frames++;
        perf_frame_end(sys_now() - t0);
        if (sys_keys[KEY_ESC] || (bench && ticks >= bench)) break;
        disp_flip();                                            /* waits for the vertical blank */
    }
    sys_give();
    disp_close();
    sys_free_all();
```

The tick loop keeps game time exact at any display rate: run at 50 Hz and at 60 Hz it gave the same game-state checksum after 500 ticks, in 500 and 600 frames.

### 2. A scrolling tile map

From `examples/demo_scroll.c`. The display keeps one hidden strip beside the view. New tile columns are drawn into it a few tiles per frame, before they scroll in.

```c
static const DispCfg display = { .planes = 6, .scroll = 1, .fetch = 1, .view_h = 256, .buf_h = 256,
                                 .level_w = MAP_W * 16, .buffers = 3, .double_y = 0, .sprite_w = 16, .sprite_bank = 4 };

#define TILES_PER_FRAME 4               /* must be at least  rows * scroll speed / 16, rounded up */
static WORD col_lo, col_hi;             /* map columns col_lo..col_hi are complete in the buffers */
static WORD work_col = -1, work_row;    /* the column being drawn, a few tiles per frame */

static void put_tile(int c, int r) { disp_tile(c * 16, r * 16, tiles + map[r][c] * PLANES * 16); }

/* Call once per frame. Works in both directions. */
static void scroll_tiles(LONG cam)
{
    WORD want_lo = (cam - 32) >> 4, want_hi = (cam + 320 + 32) >> 4, n = TILES_PER_FRAME;
    if (want_lo < 0) want_lo = 0;
    if (want_hi > MAP_W - 1) want_hi = MAP_W - 1;
    if (col_lo < want_lo - 2) col_lo = want_lo - 2;     /* columns far behind are forgotten: the buffer */
    if (col_hi > want_hi + 2) col_hi = want_hi + 2;     /* row is only 512 pixels wide and reuses them  */
    while (n--) {
        if (work_col < 0) {
            if (col_hi < want_hi) work_col = col_hi + 1;
            else if (col_lo > want_lo) work_col = col_lo - 1;
            else return;
            work_row = 0;
        }
        put_tile(work_col, work_row);
        if (++work_row == MAP_H) {
            if (work_col > col_hi) col_hi = work_col; else col_lo = work_col;
            work_col = -1;
        }
    }
}
```

At start, draw columns 0 to 22 and set `col_lo = 0; col_hi = 22;`. Then each frame:

```c
    disp_begin();                       /* erase the objects from two frames ago */
    scroll_tiles(cam);
    for (i = 0; i < NBOB; i++) disp_bob(world_x[i], world_y[i], image, mask, 2, 32);
    for (i = 0; i < 5; i++) disp_sprite(i, sprite[i], screen_x[i], screen_y[i], 16, 0);
    disp_scroll(cam, 0);
```

Load the sprites' colours as well as the playfield's: with `sprite_bank` 4 that is `disp_palette(sprite_colours, 64, 16);`.

Call `disp_bob` only for objects within 64 pixels of the view. Vertical scrolling works by passing `y` to `disp_scroll` with `buf_h` as tall as the level, up to about 512 lines.

A level that loops: do not wrap `x` back to 0. The picture slides through memory as `x` grows, so let `x` keep growing and read the map with the column number modulo the map width. Set `level_w` to the longest distance one game can scroll: each 8 pixels of it cost one byte per plane per buffer.

- Draw every tile of a new column, empty ones included. `disp_tile` is opaque, and the strip being filled still holds old picture data. For a tile with see-through pixels, draw the tile behind it first, then `disp_stamp` the see-through one on top.
- `x` must stay between 0 and `level_w` minus 320. Past that the view leaves the buffer. One buffer takes planes x (64 x `buf_h` + `level_w` / 8 + 64) bytes.
- A game that can scroll further than `level_w`: at a quiet moment set `x` back to a small value and redraw the 22 visible columns. That one-off redraw takes several frames.

### Still screens

From `examples/demo_still.c`. A still screen is a full picture pasted once, pieces stamped onto it, and `disp_bob` or sprites for what moves. Erase a piece by pasting that part of the picture again.

```c
    disp_paste(backdrop, 0, 0, 320, 256);                       /* whole background, once, after disp_on() */
    for (i = 0; i < 10; i++) disp_stamp(20 + i * 20, 40, brick_img, brick_msk, 2, 8);   /* pieces, any x */
    disp_paste(backdrop, 48, 32, 48, 32);                       /* erase: x and width in multiples of 16 */
```

`backdrop` is what `gfx.picture(pixels, 320, 256, planes)` writes, linked into chip RAM.

`disp_paste` works in 16-pixel columns. When pieces sit side by side, erase the 16-aligned rectangle around the piece, then stamp again every neighbour that rectangle touched.

### 3. Converting graphics on the host

`tools/gfx.py` takes byte-per-pixel images as Python lists. This is the pattern used on a PC game's tiles and sprites in the lab:

```python
import sys; sys.path.insert(0, 'tools'); import gfx
images, pal, mapping = gfx.compact(tiles + [player], pc_palette)   # renumber used colours to 0..n-1
planes = gfx.planes_needed(len(pal))                              # 53 colours gave 6 planes
open('assets/tiles.dat', 'wb').write(b''.join(gfx.tile(t, planes) for t in images[:len(tiles)]))
img, msk, wwords = gfx.bob(images[-1], 32, 16, planes)            # masked object for disp_bob
open('assets/player.img', 'wb').write(img); open('assets/player.msk', 'wb').write(msk)
even, odd = gfx.sprite_pair(enemy, 16, 16)                        # 15-colour sprite pair, pixel values 0..15
open('assets/enemy0.spr', 'wb').write(even); open('assets/enemy1.spr', 'wb').write(odd)
open('assets/game.pal', 'wb').write(gfx.palette(pal))             # 0x00RRGGBB longs for disp_palette
```

Link the files into chip RAM with a small assembly file, and declare them in C as `extern UWORD tiles[];`:

```asm
        section gfx,data_c              ; chip RAM
        xdef    _tiles,_player_img,_player_msk,_enemy0,_enemy1,_game_pal
        cnop    0,8
_tiles:      incbin "assets/tiles.dat"
        cnop    0,8
_player_img: incbin "assets/player.img"
        cnop    0,8
_player_msk: incbin "assets/player.msk"
        cnop    0,8
_enemy0:     incbin "assets/enemy0.spr"
        cnop    0,8
_enemy1:     incbin "assets/enemy1.spr"
        cnop    0,8
_game_pal:   incbin "assets/game.pal"
        cnop    0,8
        dcb.b   64,0                    ; the blitter reads a little past the last object
```

An attached pair is shown with two calls at the same position: `disp_sprite(0, enemy0, x, y, 16, 0); disp_sprite(1, enemy1, x, y, 16, 1);`

A sprite block is (h + 2) x `sprite_w` / 4 bytes. Give every sprite number its own copy: `spr[i] = sys_chip_copy(enemy0, (16 + 2) * 16 / 4);`. The copy also has the alignment that `sprite_w` 32 and 64 need. `cnop` and `__chip` cannot guarantee that alignment once the program has several files with chip data.

### 4. Per-pixel rendering

From `examples/demo_chunky.c`. The byte buffer is 160 x 128 and every pixel is shown double size. The conversion routines are Mikael Kalms' public-domain C2P.

```c
static const DispCfg display = { .planes = 8, .scroll = 0, .fetch = 3, .view_h = 256, .buf_h = 128,
                                 .level_w = 0, .buffers = 2, .double_y = 1, .sprite_w = 16, .sprite_bank = 1 };

void c2p2x1_8_c5_gen_init(int cx __asm("d0"), int cy __asm("d1"), int offx __asm("d2"), int offy __asm("d3"),
                          int rowlen __asm("d4"), int bplsize __asm("d5"), int chunkylen __asm("d6"));
void c2p2x1_8_c5_gen(void *chunky __asm("a0"), void *planes __asm("a1"));

    chunky = sys_alloc(160 * 128, MEMF_ANY | MEMF_CLEAR);        /* in game_load(): fast RAM if there is any */
    c2p2x1_8_c5_gen_init(160, 128, 0, 0, disp_row, disp_plane, 160);   /* once, after sys_take() */

    render(chunky);                                             /* each frame */
    c2p2x1_8_c5_gen(chunky, disp_back);
    disp_flip();
```

Add `kit/c2p/c2p2x1_8_c5_gen.s` to the build line. In the renderer, build four pixels in a register and store one long: a byte store costs as much as a long store.

The width given to `c2p2x1_8_c5_gen_init` must be a multiple of 32: 160, 128 or 96. The routine rounds other widths down without a warning.

### 5. Music and sound effects

Frank Wille's public-domain ptplayer (`kit/ptplayer/ptplayer.asm`) plays ProTracker modules and mixes sound effects into free channels. It ran under the kit at 0.5% of the CPU.

```c
void mt_install(void *custom_base __asm("a6"), void *vbr __asm("a0"), UBYTE pal __asm("d0"));
void mt_remove(void *custom_base __asm("a6"));
void mt_init(void *custom_base __asm("a6"), void *module __asm("a0"), void *samples __asm("a1"), UBYTE pos __asm("d0"));
void mt_end(void *custom_base __asm("a6"));
typedef struct { void *sfx_ptr; UWORD sfx_len, sfx_per, sfx_vol; BYTE sfx_cha, sfx_pri; } SfxStructure;
void *mt_playfx(void *custom_base __asm("a6"), SfxStructure *sfx __asm("a0"));
extern UBYTE mt_Enable;

    mt_install((void *)0xdff000, sys_vbr, sys_is_pal);          /* after sys_take() */
    mt_init((void *)0xdff000, module, 0, 0);
    mt_Enable = 1;                                              /* music plays from an interrupt */

    fx.sfx_ptr = shot; fx.sfx_len = 200; fx.sfx_per = 322; fx.sfx_vol = 64; fx.sfx_cha = -1; fx.sfx_pri = 10;
    mt_playfx((void *)0xdff000, &fx);                           /* any time during the game */

    mt_Enable = 0; mt_end((void *)0xdff000); mt_remove((void *)0xdff000);   /* before sys_give() */
```

The module and every sample must be in chip RAM. A sample is signed 8-bit, starts with two zero bytes, and `sfx_len` counts 16-bit words. `sfx_per` is 3,546,895 divided by the sample rate: 322 plays 11,025 Hz. Add `kit/ptplayer/ptplayer.asm` to the build line.

Sound effects work without music: call `mt_install`, leave `mt_Enable` at 0 and use `mt_playfx`. `tools/gfx.py` has `sound(values, rate)`, which returns the sample bytes with the two leading zeros, the period and the length in words.

### 6. Loading a file

Only inside `game_load()`, before `sys_take()`:

```c
    ULONG size;
    UBYTE *level = sys_load("data/level1.bin", MEMF_ANY, &size);    /* MEMF_CHIP for graphics and samples */
    if (!level) return 0;
```

Small games need no files at all: `incbin` everything as in piece 3.

## Prove the speed

A port is at full speed when its own `bench.txt` says so, not when it looks smooth. The kit writes that file for you.

### The bench run

- `mygame bench N` must play N logic ticks from scripted input, then quit and write `bench.txt`. The skeleton does this already.
- Write `game_bench_input(tick)` so the run passes through the busiest scenes: the most objects, the fastest scrolling, the largest enemy. A bench of an empty level proves nothing.
- Return a checksum of the whole game state from `game_checksum()`. It lands in `bench.txt` and must equal the host test's value.
- Run at least 3,000 ticks, which is one minute.

### Reading bench.txt

```text
frames=500 late=0 worst_cck=2561 avg_cck=2549 budget_cck=71051 worst_pct=3 avg_pct=3 display_hz=50 shown_fps=50.0
ticks=500 checksum=00235400
```

| Field | Meaning | Pass |
| --- | --- | --- |
| `late` | Vertical blanks that went by without a new picture | 0 |
| `worst_pct` | The slowest frame's work as a share of the frame | 80 or less. 60 or less if inner loops multiply or divide |
| `shown_fps` | Pictures shown per second | 50.0. For per-pixel games: report the number |
| `checksum` | Game state after N ticks | Same as the host test |

A per-pixel game passes on an equal checksum and an honest `shown_fps`: its `late` count is large by design. Record the emulator version from `fs-uae --version` in the hand-back.

### Emulator settings that count

- FS-UAE with `amiga_model = A1200` and nothing else changed: 68EC020, cycle-exact, 2 MB chip RAM, no fast RAM. `tools/bench.sh` uses exactly this.
- Never measure with JIT, with "fastest possible" CPU speed, or with fast RAM added. Those numbers say nothing about a real A1200.
- Run the bench a second time with `--fast_memory=4096` and report both. Many real machines have that upgrade.
- Look at the screenshot `bench.sh` saves. A fast black screen is not a port.

### What the emulator gets wrong

Its memory timing follows the hardware manuals: the lab's measurements matched the calculated values within 3%. Its arithmetic timing does not. Multiply, divide, shifts and indexed addressing are charged less than a real 68EC020 takes, and a misaligned long in chip RAM costs nothing extra in FS-UAE 3.1. So an emulator result is an upper bound. That is why the limit is 80%, not 100%. The final judge is `mygame bench 3000` on a real A1200: say so in the hand-back.

## Mistakes to avoid

Each of these either happened while this brief was being tested or is a known way ports die. Check the list before the final bench.

### Design

- Redrawing the whole picture every frame. See the one idea at the top.
- Using a byte-per-pixel buffer for a 2D game because the original had one. Tiles, sprites and `disp_bob` objects are the 2D route.
- Keeping 256 colours when the art uses 60. Count the colours first.
- Large objects everywhere. One 64 x 64 object in 6 planes is a quarter of the frame.
- Drawing the score, lives or a status bar every frame. Draw digits only when the value changes, or use sprites.
- Emulating the source machine instead of translating the game.

### Code

- `float`, `double`, `sin`, `sqrt`, division or `%` in per-frame code.
- Byte loops over memory, and `int` fields at offsets not divisible by 4 in structs that are touched every frame.
- Calling `printf`, `malloc`, `AllocMem`, `fopen` or any AmigaOS function between `sys_take()` and `sys_give()`. The machine hangs or crashes.
- Large arrays as local variables. The stack is 4 KB.
- Reading PC data files without swapping byte order, or through a struct.
- Leaving 60 Hz constants unchanged at 50 ticks a second: the game runs 17% slow.
- Counting frames instead of ticks for game time. When a frame is late, the game must still advance: the skeleton loop does this.
- Returning from `main` without `sys_give()` on an error path after `sys_take()`.

### Hardware

- Writing your own display set-up. Three traps cost hours in the lab: the fetch-stop value differs per fetch mode ($D0, $C8 or $B8), bitplanes must be 8-byte aligned for 64-bit fetch, and the raster counter ticks two colour clocks after the line starts. The kit handles all three.
- Graphics, sprites or samples outside chip RAM. The blitter and the display read garbage. Use `__chip`, `data_c` or `MEMF_CHIP`.
- Using sprites 5 to 7 on a scrolling screen with `fetch` 1, or any sprite but 0 with `fetch` 3. They do not appear.
- Giving `disp_tile` an x that is not a multiple of 16, or asset data that is not big-endian.
- Starting a blit of your own without `blit_wait()` first.
- Sprite or object colours from the wrong palette range. Check `sprite_bank` and the pair rule in Step 2.

### Claims

- Reporting a frame rate from a fast emulator mode, or from watching the screen.
- Calling a port done without `late=0` in `bench.txt`.
- Promising 50 frames a second for a per-pixel game on a stock machine.

## What to hand back

Hand back the project folder and a port report in exactly this form. Every number in it must come from a file the project can produce again.

```text
PORT REPORT
Game:                  <name>
Class and route:       <the row of the Step 1 table you used>
Display:               planes= scroll= fetch= buffers= sprites used= colours=
Busiest moment:        <what is on screen> = <budget sum>%
Cut from the original: <list, or "nothing">
Stock A1200 bench:     <both lines of bench.txt>  (<emulator name and version>)
With 4 MB fast RAM:    <both lines of bench.txt>
Host checksum:         <value after the same number of ticks>  equal / not equal
Frame rate claim:      <"50 Hz held" or "N fps shown, logic at 50 ticks">
Not done, not tested:  <real hardware, real Kickstart, NTSC, anything skipped>
How to run:            <files in dist/, the command, the keys>
```

- Write "not measured" where you have no number. Never estimate in this report.
- The folder must rebuild with `tools/build.sh` and re-measure with `tools/bench.sh`.
- If a gate fails and you cannot fix it, stop there and hand back with the failing output. A truthful partial port is useful. A finished port with invented numbers is not.

## Sources and what was verified

The numbers in this brief come from measurement on one emulator, checked against hardware documents. Nothing here ran on a real A1200.

### Measured for this brief

Rig: FS-UAE 3.1.66 (WinUAE 3.3.0b2 core), A1200 model, cycle-exact 68EC020, 2 MB chip RAM, the free AROS ROM. Compiler: m68k-amigaos-gcc 6.5.0b from the `amigadev/crosstools` Docker image.

- Frame size, and the cost of CPU reads, writes and copies with the display off and at 4, 5, 6 and 8 planes in each fetch mode.
- The 320-pixel display in all four fetch modes, including the fetch-stop values, with identical screenshots.
- Blitter and CPU costs for clears, copies, tiles and masked objects of 16, 32 and 64 pixels.
- The C2P routines at five sizes, with and without fast RAM, and line doubling.
- Hardware scrolling at twelve positions in three fetch modes, and which sprites survive in each.
- Sprites 16, 32 and 64 pixels wide, a 15-colour attached pair, and where a sprite lands for a given x and y.
- Joystick and keyboard code, with input injected into the emulator.
- Switching between 50 and 60 Hz, and the skeleton loop keeping the same game state on both.
- The ptplayer music library running under the kit.
- Every kit call in the cost table, and the demos' bench results.
- Type sizes, struct layout and chip-RAM placement in this compiler.

### Documents

- [MC68020 User's Manual](https://www.nxp.com/docs/en/data-sheet/MC68020UM.pdf): instruction timing, cache, alignment.
- [A1200 functional specification, Commodore 1992](https://aminet.net/docs/hard/A1200FuncSpec.lha): CPU, clock, memory, expansion limits.
- [Amiga Hardware Reference Manual](https://scratch.grimore.org/HARDWARE_MANUAL_GUIDE/): blitter speed, bus slot allocation.
- [Commodore AGA notes in the RandyAGA collection](https://aminet.net/docs/misc/RandyAGA.lha): fetch modes, scroll bits, sprite widths and colour banks.
- [Individual Computers accelerator benchmarks](https://wiki.icomp.de/wiki/Template:ACA_Benchmarks) and the AIBB reviews under [Aminet docs/rview](https://aminet.net/docs/rview): real-machine speed with fast RAM and accelerators.
- [WinUAE source](https://github.com/tonioni/WinUAE): how the emulator charges time.
- [Kalms C2P](https://github.com/Kalmalyzer/kalms-c2p), [ptplayer](https://aminet.net/package/mus/play/ptplayer), [amiga68ktools](https://github.com/jotd666/amiga68ktools), [amigadev/crosstools](https://hub.docker.com/r/amigadev/crosstools), [FS-UAE](https://fs-uae.net).

### Trial with weaker models

Agents on smaller models each ported a small PC game written for this test, in two rounds. They were given this brief, the kit and the game's source, and nothing else. Their benches were re-run independently and the screens compared with the originals.

| Round | Game and class | Model | Result on the emulated stock A1200 | Checksum, host against Amiga |
| --- | --- | --- | --- | --- |
| 1 | Scrolling shooter: tile map, enemies, bullets, explosions, score | Sonnet | 50 Hz held for 3,000 frames, none late, worst frame 30% | equal |
| 1 | Ray-caster: per-pixel | Sonnet | 9.5 frames a second, 12.3 with fast RAM, logic at 50 ticks | equal |
| 1 | Breakout: still screen | Haiku | 49.9 frames a second, 3 late frames in 2,500, worst frame 107% | equal |
| 2 | Breakout again, revised brief and kit | Haiku | 50 Hz held for 2,500 frames, none late, worst frame 36% | equal |
| 2 | Scrolling shooter | Haiku | 50 Hz held for 3,000 frames, none late, worst frame 41%. Stars, status bar and sound were cut | equal |

Every report stated its own shortfalls and cuts. In round 1 the Breakout port missed the gate because the kit then had no call for changing part of a still background: it repainted tiles by CPU. `disp_paste` and `disp_stamp` were added and measured after that trial, along with the other gaps the three agents listed.

### Independent review

A separate expert pass read the kit and this brief for faults an emulator run cannot show. It found 21 and all are fixed in the kit and in this text. The fixed kit was rebuilt and its demos re-run. Three of the trial ports were then rebuilt against it unchanged: all still held 50 Hz with the same checksums. The three serious ones:

- Memory was never freed on exit, so each run lost its memory until reboot and a large game would soon fail to start. Fixed with `sys_alloc` and `sys_free_all`, then checked: free memory is the same after one, two and three runs.
- Objects were not clipped at the left and right screen edge and could write outside the buffer. Fixed, then checked at every edge by measuring a screenshot.
- Sprite pointers were written after the display had already read them, one frame late. The display list now reads them after the kit has written them.

The review also checked, against the current emulator source, the fetch values, the sprite counts when scrolling, line doubling, the sprite data layout, the blitter scheme and the keyboard handshake. It found nothing wrong in those.

### Not verified

- Real hardware and a Commodore Kickstart ROM. The takeover code follows the standard sequence but ran only under the AROS ROM.
- The 68030 figures: they come from published benchmarks and the C2P author's own note, not from this lab.
- The Z80, 6502, 6809 and 68000-machine routes in Step 1: described from the tools' own documentation, not exercised here.
- A second emulator. The planned cross-check was not run.
- Endless vertical scrolling, dual playfield and NTSC screen layout: not in the kit.
