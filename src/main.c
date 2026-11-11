/* main.c - skeleton of a full-speed Amiga 1200 game. Replace the game_* functions; keep main() as it is.
 *
 *   game             play; Escape quits
 *   game bench N     play N logic ticks from scripted input, quit, write bench.txt (the proof of speed)
 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"
#include "disp.h"
#include "perf.h"
#include "logic.h"

#undef TICKS_PER_SECOND
#define TICKS_PER_SECOND 60             /* the NES runs at 60 frames a second: 6 ticks every 5 frames */

/* ---- the game ------------------------------------------------------------------------------ */
/* The game logic is the NES program translated to C (src/logic.c, src/gen_*.c): game_tick, game_bench_input
   and game_checksum live there. This file only shows the NES picture on the AGA display. */

static const DispCfg display = { 4, 1, 0, 240, 480, 8192, 3, 0, 16, 1 };
static ULONG palette[48];

static int game_load(void)
{
    game_init();
    return 1;
}

static void game_start(void)
{
}

static void game_draw(void)
{
    disp_begin();
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
#ifdef LOGIC_TRACE
        {   extern uint32_t logic_trace[]; extern unsigned logic_ntrace;
            BPTR fh = Open((CONST_STRPTR)"trace.bin", MODE_NEWFILE);
            if (fh) { Write(fh, logic_trace, logic_ntrace * 4); Close(fh); }
            {   extern uint8_t logic_dump[]; fh = Open((CONST_STRPTR)"dump.bin", MODE_NEWFILE);
                if (fh) { Write(fh, logic_dump, 0x808); Close(fh); } }
        }
#endif
    }
    return 0;
}
