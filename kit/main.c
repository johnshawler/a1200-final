/* main.c - skeleton of a full-speed Amiga 1200 game. Replace the game_* functions; keep main() as it is.
 *
 *   game             play; Escape quits
 *   game bench N     play N logic ticks from scripted input, quit, write bench.txt (the proof of speed)
 */
#include <proto/exec.h>
#include <exec/memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"
#include "disp.h"
#include "perf.h"

#define TICKS_PER_SECOND 50             /* game logic rate. For a 60 Hz original use 60: see main() */

/* ---- the game ------------------------------------------------------------------------------ */

static const DispCfg display = { 5, 0, 3, 256, 256, 0, 3, 0, 16, 2 };   /* 32 colours, fixed screen, sprites use 32..47 */
static UWORD *ship;                     /* a 16x16 sprite, chip RAM */
static UWORD *block_img, *block_msk;    /* a 16x16 object for the blitter, chip RAM */
static UWORD *ground_tile;              /* a 16x16 background tile, chip RAM */
static ULONG palette[48];
static LONG ship_x = 152 << 8, ship_y = 200 << 8;       /* positions are 24.8 fixed point: never float */
static LONG block_x = 20 << 8, block_dx = 384;

/* Runs BEFORE takeover, the only place where AmigaOS still works: open files, AllocMem, build tables. */
static int game_load(void)
{
    int i, y;
    ship      = sys_alloc(4 + 16 * 4 + 4, MEMF_CHIP | MEMF_CLEAR);
    block_img = sys_alloc(5 * 16 * 2 + 8, MEMF_CHIP | MEMF_CLEAR);
    block_msk = sys_alloc(16 * 2 + 8, MEMF_CHIP | MEMF_CLEAR);
    ground_tile = sys_alloc(5 * 16 * 2, MEMF_CHIP | MEMF_CLEAR);
    if (!ship || !block_img || !block_msk || !ground_tile) return 0;
    for (i = 0; i < 5 * 16; i++) ground_tile[i] = (i < 16) ? 0xaaaa : (i < 48) ? 0xffff : 0x0f0f;
    for (y = 0; y < 16; y++) { ship[2 + y * 2] = 0xffff >> (15 - y); ship[3 + y * 2] = 0x0ff0; }      /* 2 words per line */
    for (i = 0; i < 5 * 16; i++) block_img[i] = (i / 16 == 0 || i / 16 == 3) ? 0xffff : 0x5555;
    for (i = 0; i < 16; i++) block_msk[i] = 0xffff;
    for (i = 0; i < 32; i++) palette[i] = ((ULONG)(i * 8) << 16) | ((ULONG)(i * 5) << 8) | 0x40;
    palette[33] = 0xffffff; palette[34] = 0xff4040; palette[35] = 0xffe040;
    return 1;
}

/* Runs once AFTER takeover: draw the starting picture (backgrounds go into every buffer). */
static void game_start(void)
{
    int x;
    for (x = 0; x < 320; x += 16) disp_tile(x, 240, ground_tile);
}

/* One step of game logic = 1/TICKS_PER_SECOND of game time. No drawing here. */
static void game_tick(UWORD joy)
{
    if (joy & JOY_LEFT)  ship_x -= 2 << 8;
    if (joy & JOY_RIGHT) ship_x += 2 << 8;
    if (joy & JOY_UP)    ship_y -= 2 << 8;
    if (joy & JOY_DOWN)  ship_y += 2 << 8;
    if (ship_x < 0) ship_x = 0;
    if (ship_x > (304 << 8)) ship_x = 304 << 8;
    if (ship_y < 0) ship_y = 0;
    if (ship_y > (240 << 8)) ship_y = 240 << 8;
    block_x += block_dx;
    if (block_x < (8 << 8) || block_x > (296 << 8)) block_dx = -block_dx;
}

/* Bring the picture up to date. Touch only what changed. */
static void game_draw(void)
{
    disp_begin();                                               /* erases what was drawn into this buffer two frames ago */
    disp_bob(block_x >> 8, 100, block_img, block_msk, 1, 16);   /* blitter object */
    disp_sprite(0, ship, ship_x >> 8, ship_y >> 8, 16, 0);      /* hardware sprite: free */
}

/* Input for bench mode, as a function of the tick number. It must drive the game through its busiest scenes. */
static UWORD game_bench_input(ULONG tick)
{
    return ((tick / 64) & 1 ? JOY_LEFT : JOY_RIGHT) | ((tick / 40) & 1 ? JOY_UP : JOY_DOWN);
}

/* A number that depends on the whole game state. The host build of the logic must print the same value. */
static ULONG game_checksum(void)
{
    return (ULONG)ship_x * 31 + (ULONG)ship_y * 17 + (ULONG)block_x;
}

/* ---- the loop: keep as it is ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    ULONG bench = (argc > 2 && !strcmp(argv[1], "bench")) ? strtoul(argv[2], 0, 10) : 0;
    ULONG last, now, vblanks, t0, frames = 0, ticks = 0, acc = 0, hz;
    char text[40];

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
    if (bench) {
        sprintf(text, "ticks=%lu checksum=%08lx", (unsigned long)ticks, (unsigned long)game_checksum());
        perf_note(text);
        perf_save("bench.txt");
    }
    return 0;
}
