/* logic.c - the game's logic pile: the translated 6502 program (src/gen_*.c, made by tools/recomp.py) run one
 * NES frame per tick. Plain C with no platform header: the host test compiles exactly this.
 *
 * The same file runs the C translation on the host (src/run_c.c) and the 68k one on the Amiga (src/run68k.c).
 *
 * One tick = what the NES does between two vertical blanks: the NMI handler, then the main thread until it
 * waits for the next frame again. The main thread runs as a coroutine and gives control back at its wait loops.
 */
#include "nes.h"
#include "coro.h"
#include "logic.h"
#include "run.h"

extern const uint8_t nes_prg_data[];            /* 128 KB PRG ROM: assets/prg.bin */
extern const uint8_t nes_chr_data[];            /* 128 KB CHR ROM: assets/chr.bin */

uint8_t logic_extra_pad;
#ifdef LOGIC_TRACE                              /* debug builds: the checksum after every tick */
uint32_t logic_trace[LOGIC_TRACE]; unsigned logic_ntrace;
uint8_t logic_dump[0x808];
uint32_t logic_time[LOGIC_TRACE];               /* colour clocks each tick took (Amiga debug builds) */
uint32_t (*logic_clock)(void);
#endif
uint32_t logic_frames;
static uint32_t co_stack[32768 / 4];            /* the NES main thread's C stack */

static void do_nmi(uint16_t pc)
{
    uint8_t p = (uint8_t)((cpu_n & 0x80) | (cpu_v ? 0x40 : 0) | 0x20 | (cpu_d ? 8 : 0) | (cpu_i ? 4 : 0) | (cpu_z ? 0 : 2) | cpu_c);
    nes_ram[0x100 + cpu_s] = (uint8_t)(pc >> 8); cpu_s--;
    nes_ram[0x100 + cpu_s] = (uint8_t)pc; cpu_s--;
    nes_ram[0x100 + cpu_s] = p; cpu_s--;
    cpu_i = 1;
    split_seen = 0;
    if (run_nmi() != pc) nes_fatal(pc, 4);
    split_on = split_seen;
}

/* Called by the translated code in a loop that waits for the next frame. */
void nes_wait(int kind, uint16_t pc)
{
    co_yield();                                 /* the frame is over: the Amiga side draws it */
    logic_frames++;
    if (ppu_ctrl & 0x80) do_nmi(pc);            /* vertical blank */
    if (kind == WAIT_VBL) ppu_vbl = 1;          /* a $2002 poll sees the flag after the NMI */
}

static void nes_main(void)
{
    uint16_t pc = run_reset();
    for (;;) {
        if (nes_fatal_why) co_yield();          /* stopped: keep the frame loop alive */
        else pc = run_dispatch(pc);             /* a return nobody called from: continue there */
    }
}

void game_init(void)
{
    nes_init(nes_prg_data, nes_chr_data);
    co_init(nes_main, co_stack, sizeof co_stack);
}

void game_tick(unsigned short in)
{
    uint8_t p = logic_extra_pad;
    if (in & IN_A) p |= 0x01;
    if (in & IN_B) p |= 0x02;
    if (in & IN_SELECT) p |= 0x04;
    if (in & IN_START) p |= 0x08;
    if (in & IN_UP) p |= 0x10;
    if (in & IN_DOWN) p |= 0x20;
    if (in & IN_LEFT) p |= 0x40;
    if (in & IN_RIGHT) p |= 0x80;
    nes_pad = p;
#ifdef LOGIC_TRACE
    {   uint32_t t0 = logic_clock ? logic_clock() : 0;
        co_resume();
        if (logic_ntrace < LOGIC_TRACE) logic_time[logic_ntrace] = logic_clock ? logic_clock() - t0 : 0; }
#else
    co_resume();
#endif
#ifdef LOGIC_TRACE
#ifdef LOGIC_NOSUM                              /* profiling builds: no checksum work in the ticks */
    if (logic_ntrace < LOGIC_TRACE) logic_trace[logic_ntrace++] = 0;
#else
    if (logic_ntrace < LOGIC_TRACE) logic_trace[logic_ntrace++] = game_checksum();
#endif
#ifdef LOGIC_DUMP
    if (logic_ntrace == LOGIC_DUMP) { int i; for (i = 0; i < 0x800; i++) logic_dump[i] = nes_ram[i];
        logic_dump[0x800] = cpu_a; logic_dump[0x801] = cpu_x; logic_dump[0x802] = cpu_y; logic_dump[0x803] = cpu_s;
        logic_dump[0x804] = cpu_c; logic_dump[0x805] = cpu_z; logic_dump[0x806] = cpu_n; logic_dump[0x807] = cpu_v; }
#endif
#endif
}

/* Bench: "Exhibition mode, watch game" (computer against computer), then a tap on A every 128 ticks to get
   through the screens between plays. It plays pitches, hits and fielding: the busiest scenes. */
unsigned short game_bench_input(unsigned int t)
{
    /* BENCH INPUT BEGIN (per game: tools/new_port.sh resets it) */
    if (t >= 100 && t <= 105) return IN_START;
    if (t >= 200 && t <= 205) return IN_DOWN;
    if (t >= 250 && t <= 255) return IN_A;
    if (t >= 330 && t <= 335) return IN_DOWN;
    if (t >= 360 && t <= 365) return IN_DOWN;
    if (t >= 400 && t <= 405) return IN_A;
    if (t >= 500 && (t & 127) < 4) return IN_A;
    /* BENCH INPUT END */
    return 0;
}

static uint32_t fnv(uint32_t h, const uint8_t *p, unsigned n)
{
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

uint32_t game_checksum(void)
{
    uint32_t h = 2166136261u;
    uint8_t r[8];
    h = fnv(h, nes_ram, sizeof nes_ram);
    h = fnv(h, nes_vram, sizeof nes_vram);
    h = fnv(h, nes_pal, sizeof nes_pal);
    h = fnv(h, nes_oam, sizeof nes_oam);
    r[0] = cpu_a; r[1] = cpu_x; r[2] = cpu_y; r[3] = cpu_s; r[4] = nes_bank; r[5] = ppu_ctrl; r[6] = ppu_mask; r[7] = (uint8_t)nes_fatal_why;
    return fnv(h, r, 8);
}
