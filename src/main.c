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
#include "video.h"
#include "sound.h"

#undef TICKS_PER_SECOND
#define TICKS_PER_SECOND 60             /* the NES runs at 60 frames a second: 6 ticks every 5 frames */

/* ---- the game ------------------------------------------------------------------------------ */
/* The game logic is the NES program translated to C (src/logic.c, src/gen_*.c): game_tick, game_bench_input
   and game_checksum live there. This file only shows the NES picture on the AGA display. */

#define display video_display          /* src/video.c: 4 planes, scrolling, 64-pixel sprites */
static ULONG palette[48];
#ifdef LOGIC_TRACE
uint32_t video_time_sum, video_time_max, video_calls;
#endif               /* all black until the game sets its colours */

static int game_load(void)
{
#ifdef LOGIC_TRACE
    { extern uint32_t (*logic_clock)(void); logic_clock = (uint32_t (*)(void))sys_now; }
#endif
    game_init();
    return video_load() && sound_load();
}

#ifdef LOGIC_TRACE
/* debug: a sampling profiler. At every vertical blank, find the interrupted program counter in the exception
   frame (SR with the supervisor byte clear, PC, format word $006C for the level 3 vector) and keep it. */
ULONG prof_pc[16384], prof_n;
ULONG frame_log[16384], frame_n;          /* debug: vertical blank and tick at every drawn frame */
static void prof_hook(void)
{
    UWORD *w;
    int i;
    sys_key_poll();
    __asm__ volatile ("move.l %%sp,%0" : "=a"(w));
    for (i = 0; i < 80; i++)
        if (w[i + 3] == 0x006c && (w[i] & 0xff00) == 0) { if (prof_n < 16384) prof_pc[prof_n++] = ((ULONG)w[i + 1] << 16) | w[i + 2]; break; }
}
#endif

static void game_start(void)
{
#ifdef LOGIC_TRACE
    { extern void prof_install(void); prof_install(); }
#endif
    video_start();
    sound_start();
}

/* Keys for the NES buttons the joystick does not have (bits as in nes_pad): A, B, Select, Start, pad */
static void read_keys(void)
{
    UBYTE p = 0;
    if (sys_keys[0x32]) p |= 0x01;                  /* X      = A */
    if (sys_keys[0x31] || sys_keys[0x64]) p |= 0x02; /* Z, Alt = B */
    if (sys_keys[KEY_SPACE]) p |= 0x04;             /* Space  = Select */
    if (sys_keys[KEY_RET] || sys_keys[0x43]) p |= 0x08; /* Return = Start */
    if (sys_keys[KEY_UP]) p |= 0x10;
    if (sys_keys[KEY_DOWN]) p |= 0x20;
    if (sys_keys[KEY_LEFT]) p |= 0x40;
    if (sys_keys[KEY_RIGHT]) p |= 0x80;
    logic_extra_pad = p;
}

static void game_draw(void)
{
    disp_begin();
#ifdef LOGIC_TRACE
    {   extern uint32_t video_time_sum, video_time_max, video_calls; ULONG t0 = sys_now(), dt;
        video_frame(); dt = sys_now() - t0; if (logic_frames >= 2000 && logic_frames < 2500) { video_time_sum += dt; video_calls++; } if (dt > video_time_max) video_time_max = dt; }
#else
    video_frame();
#endif
    sound_frame();
    read_keys();
#ifdef LOGIC_TRACE
    { extern ULONG frame_log[], frame_n; if (frame_n < 8192) { frame_log[frame_n * 2] = sys_frame; frame_log[frame_n * 2 + 1] = logic_frames; frame_n++; } }
#endif
#ifdef HOLD_AT                          /* debug: freeze the picture at a tick, for a screenshot */
    if (logic_frames >= HOLD_AT) { int i; disp_flip(); for (i = 0; i < 50 * 120 && !sys_keys[KEY_ESC]; i++) sys_wait_frame(); }
#endif
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
#ifdef LOGIC_TRACE
    { extern void prof_remove(void); prof_remove(); }      /* debug builds only: the profiler's interrupt */
#endif
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
            {   extern uint32_t video_time_sum, video_time_max, video_calls; char b[200];
                extern ULONG vprof[], vtiles;
                sprintf(b, "video avg %lu max %lu calls %lu cells %lu stream %lu sprites %lu (grouping %lu) tiles %lu\n", (unsigned long)(video_calls ? video_time_sum / video_calls : 0), (unsigned long)video_time_max, (unsigned long)video_calls,
                        (unsigned long)vprof[0], (unsigned long)vprof[1], (unsigned long)vprof[2], (unsigned long)vprof[3], (unsigned long)vtiles);
                fh = Open((CONST_STRPTR)"video.txt", MODE_NEWFILE); if (fh) { Write(fh, b, strlen(b)); Close(fh); } }
            {   extern void *reset_fn; extern ULONG prof_buf[], prof_cnt; ULONG base = (ULONG)reset_fn; fh = Open((CONST_STRPTR)"prof.bin", MODE_NEWFILE);
                ULONG cbase = (ULONG)game_tick; if (fh) { Write(fh, &base, 4); Write(fh, &cbase, 4); Write(fh, prof_buf, prof_cnt * 4); Close(fh); } }
            {   fh = Open((CONST_STRPTR)"frames.bin", MODE_NEWFILE); if (fh) { Write(fh, frame_log, frame_n * 8); Close(fh); } }
            {   extern uint32_t logic_time[]; fh = Open((CONST_STRPTR)"ticktime.bin", MODE_NEWFILE);
                if (fh) { Write(fh, logic_time, logic_ntrace * 4); Close(fh); } }
        }
#endif
    }
    return 0;
}
