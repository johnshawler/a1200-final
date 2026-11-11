/* demo_scroll.c - stress test of disp.c: 6-plane scrolling tile map, 8 blitter objects of 32x32, 5 sprites.
 *   demo_scroll bench N   run N frames, write bench.txt
 * Measured on an emulated stock A1200: 3000 frames, none late (see the brief).
 */
#include <proto/exec.h>
#include <exec/memory.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"
#include "disp.h"
#include "perf.h"

#define PLANES 6
#define MAP_W 256
#define MAP_H 16
#define NBOB 8
#define LEVEL_W (MAP_W * 16)

static UBYTE map[MAP_H][MAP_W];
static UWORD *tiles;                    /* 8 tiles x PLANES x 16 words, chip RAM */
static UWORD *bob_img, *bob_msk;        /* 32x32: PLANES x 32 x 2 words, and 32 x 2 words */
static UWORD *spr[5];
static ULONG pal[80];                   /* 64 playfield colours + the sprites' bank (64..79) */
static LONG cam, camdir = 2, bx[NBOB], by[NBOB], bdx[NBOB], bdy[NBOB];

static int load(void)
{
    int t, p, y, x, i;
    tiles   = sys_alloc(8 * PLANES * 16 * 2, MEMF_CHIP | MEMF_CLEAR);
    bob_img = sys_alloc(PLANES * 32 * 2 * 2 + 8, MEMF_CHIP | MEMF_CLEAR);
    bob_msk = sys_alloc(32 * 2 * 2 + 8, MEMF_CHIP | MEMF_CLEAR);
    for (i = 0; i < 5; i++) spr[i] = sys_alloc(4 + 16 * 4 + 4, MEMF_CHIP | MEMF_CLEAR);
    if (!tiles || !bob_img || !bob_msk || !spr[4]) return 0;
    for (t = 0; t < 8; t++)                                   /* tile t: colour (t*8 + pattern) */
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                int c = (t == 0) ? 0 : t * 8 + (((x ^ y) >> 1) & 7);
                if (x == 0 || y == 0) c = t ? t * 8 : 0;
                for (p = 0; p < PLANES; p++) if (c & (1 << p)) tiles[(t * PLANES + p) * 16 + y] |= 0x8000 >> x;
            }
    for (y = 0; y < 32; y++)                                  /* object: a shaded disc */
        for (x = 0; x < 32; x++) {
            int dx = x - 16, dy = y - 16, d2 = dx * dx + dy * dy, c;
            if (d2 > 15 * 15) continue;
            c = 56 + ((15 * 15 - d2) >> 5); if (c > 63) c = 63;
            bob_msk[y * 2 + (x >> 4)] |= 0x8000 >> (x & 15);
            for (p = 0; p < PLANES; p++) if (c & (1 << p)) bob_img[(p * 32 + y) * 2 + (x >> 4)] |= 0x8000 >> (x & 15);
        }
    for (i = 0; i < 5; i++)                                   /* sprite i: 16 lines, colour 1..3 stripes */
        for (y = 0; y < 16; y++) { spr[i][2 + y * 2] = (y & 4) ? 0xffff : 0x0ff0; spr[i][3 + y * 2] = (y & 8) ? 0xffff : 0x3c3c; }
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++) {
            int g = 11 + ((x * 7 + (x >> 3) * 5) % 4);        /* ground height varies */
            map[y][x] = (y >= g) ? 1 + ((x + y) % 3) : ((x * 13 + y * 7) % 23 == 0) ? 4 + (x % 4) : 0;
        }
    for (i = 0; i < 64; i++) pal[i] = ((ULONG)(i * 4) << 16) | ((ULONG)((i * 9) & 255) << 8) | (ULONG)(255 - i * 3);
    pal[0] = 0x101830;
    for (i = 65; i < 80; i++) pal[i] = (i & 1) ? 0xffe040 : (i & 2) ? 0xff4040 : 0xffffff;   /* sprite colours: bank 4 */
    for (i = 0; i < NBOB; i++) { bx[i] = (40 + i * 30) << 8; by[i] = (20 + i * 23) << 8; bdx[i] = 300 + i * 70; bdy[i] = 200 + i * 45; }
    return 1;
}

#define TILES_PER_FRAME 4               /* must be at least  rows * scroll speed / 16, rounded up */
static WORD col_lo, col_hi;             /* map columns col_lo..col_hi are complete in the buffers */
static WORD work_col = -1, work_row;    /* the column being drawn, a few tiles per frame */

static void put_tile(int c, int r) { disp_tile(c * 16, r * 16, tiles + map[r][c] * PLANES * 16); }

/* Call once per frame. Draws the columns that are about to scroll into view, a few tiles at a time,
   so no single frame pays for a whole column. Works in both directions. */
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

static void tick(void)
{
    int i;
    cam += camdir;
    if (cam >= LEVEL_W - 320 - 16) camdir = -2;
    if (cam <= 0) { cam = 0; camdir = 2; }
    for (i = 0; i < NBOB; i++) {
        bx[i] += bdx[i]; by[i] += bdy[i];
        if (bx[i] < 0 || bx[i] > (288 << 8)) { bdx[i] = -bdx[i]; bx[i] += bdx[i]; }
        if (by[i] < 0 || by[i] > (224 << 8)) { bdy[i] = -bdy[i]; by[i] += bdy[i]; }
    }
}

static void draw(ULONG frame)
{
    int i;
    disp_begin();
    scroll_tiles(cam);
    for (i = 0; i < NBOB; i++) disp_bob(cam + (bx[i] >> 8), by[i] >> 8, bob_img, bob_msk, 2, 32);
    for (i = 0; i < 5; i++) disp_sprite(i, spr[i], 20 + i * 60 + ((frame >> 1) & 31), 30 + i * 40, 16, 0);
    disp_scroll(cam, 0);
}

int main(int argc, char **argv)
{
    static const DispCfg cfg = { .planes = PLANES, .scroll = 1, .fetch = 1, .view_h = 256, .buf_h = 256,
                                 .level_w = LEVEL_W, .buffers = 3, .double_y = 0, .sprite_w = 16, .sprite_bank = 4 };
    ULONG bench = (argc > 2 && !strcmp(argv[1], "bench")) ? strtoul(argv[2], 0, 10) : 0;
    ULONG last, now, owed, t0, frames = 0;
    int c, r;

    if (!load() || !disp_open(&cfg)) { disp_close(); sys_free_all(); return 20; }
    if (!sys_take()) { disp_close(); sys_free_all(); return 20; }
    sys_vbl_hook = sys_key_poll;
    disp_palette(pal, 0, 80);
    disp_on();
    for (c = 0; c <= 22; c++) for (r = 0; r < MAP_H; r++) put_tile(c, r);       /* the starting picture */
    col_lo = 0; col_hi = 22;
    perf_reset();
    sys_wait_frame();
    last = sys_frame;
    for (;;) {
        t0 = sys_now();
        now = sys_frame; owed = now - last; last = now;
        if (frames == 0) owed = 1;
        perf_frame_start(owed);
        if (owed > 5) owed = 5;
        while (owed--) tick();
        draw(frames);
        frames++;
        perf_frame_end(sys_now() - t0);
        if (sys_keys[KEY_ESC] || (bench && frames >= bench)) break;
        disp_flip();
    }
    sys_give();
    disp_close();
    sys_free_all();
    if (bench) perf_save("bench.txt");
    return 0;
}
