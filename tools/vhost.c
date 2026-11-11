/* vhost.c - host only: runs the game logic and src/video.c with a fake kit display, and writes what the
 * Amiga would show (playfield + hardware sprites) as PPM pictures. For finding display bugs without the emulator.
 *   cc -Itools/hostinc -Ikit -Isrc tools/vhost.c src/video.c src/logic.c src/nes.c src/run_c.c src/coro_host.c src/gen_*.c
 *   vhost N t1,t2,...   (frames: one display frame per tick, 60 a second)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exec/types.h"
#include "disp.h"
#include "nes.h"
#include "logic.h"
#include "video.h"

uint8_t nes_prg_data[0x20000], nes_chr_data[0x20000];
UWORD disp_row, disp_margin; ULONG disp_plane; UBYTE *disp_front, *disp_back, *disp_clean;
static DispCfg cfg; static LONG scx, scy; static ULONG pal[256];
static UWORD *spr[8];
void *sys_alloc(ULONG size, ULONG f) { (void)f; return calloc(1, size); }
int disp_open(const DispCfg *c) { cfg = *c; disp_margin = 64; disp_row = 64;
    disp_plane = ((ULONG)64 * cfg.buf_h + cfg.level_w / 8 + 64 + 7) & ~7UL; disp_front = disp_back = calloc(1, disp_plane * cfg.planes + 16); return 1; }
void disp_palette(const ULONG *rgb, UWORD first, UWORD n) { int i; for (i = 0; i < n; i++) pal[first + i] = rgb[i]; }
void disp_scroll(LONG x, LONG y) { scx = x; scy = y; }
void disp_begin(void) {}
void disp_sprite(UWORD n, UWORD *d, WORD x, WORD y, UWORD h, UWORD a)
{   /* the same control words as kit/disp.c writes */
    UWORD hs = (UWORD)(x + 0x80), vs = (UWORD)(y + 0x34), ve = (UWORD)(vs + h);
    if (n > 6) return;
    spr[n] = d;
    {   UWORD p = (UWORD)(((vs & 0xff) << 8) | ((hs >> 1) & 0xff));
        UWORD c = (UWORD)(((ve & 0xff) << 8) | (a ? 0x80 : 0) | (((vs >> 8) & 1) << 2) | (((ve >> 8) & 1) << 1) | (hs & 1));
        UBYTE *b = (UBYTE *)d; b[0] = p >> 8; b[1] = (UBYTE)p; b[8] = c >> 8; b[9] = (UBYTE)c; }   /* big-endian, as on the Amiga */
}
void disp_sprite_off(UWORD n) { spr[n] = 0; }

static void shot(const char *name)
{
    static ULONG img[240][320]; int x, y, p, n; FILE *f;
    for (y = 0; y < 240; y++) for (x = 0; x < 320; x++) {
        ULONG off = (ULONG)(y + scy) * 64 + ((scx + x + 64) >> 3); int b = 7 - ((scx + x) & 7), c = 0;
        for (p = 0; p < 4; p++) c |= ((disp_front[off + p * disp_plane] >> b) & 1) << p;
        img[y][x] = pal[c];
    }
    for (n = 0; n < 7; n += 1) {            /* walk each sprite list: segments of (pos, ctl, lines) */
        UBYTE *d = (UBYTE *)spr[n], *o; int odd = n & 1;
        if (!d || (n < 6 && odd)) continue;
        o = (n < 6 && spr[n + 1]) ? (UBYTE *)spr[n + 1] : 0;
        for (;;) {
            if (d - (UBYTE *)spr[n] > 4600) { fprintf(stderr, "sprite %d list runs past its block\n", n); break; }
            UWORD pos = (UWORD)(d[0] << 8 | d[1]), ctl = (UWORD)(d[8] << 8 | d[9]);
            int vs = pos >> 8, ve = ctl >> 8, hs = ((pos & 0xff) << 1) | (ctl & 1), l;
            if (!pos && !ctl) break;
            vs |= (ctl & 4) << 6; ve |= (ctl & 2) << 7;
            if (ve <= vs) { fprintf(stderr, "sprite %d: bad segment vs %d ve %d\n", n, vs, ve); break; }
            for (l = 0; l < ve - vs; l++) for (x = 0; x < 64; x++) {
                UBYTE *line = d + 16 + l * 16, *ol = o ? o + 16 + l * 16 : 0; int bx = x >> 3, bt = 7 - (x & 7), v, X = hs - 0x80 + x, Y = vs - 0x34 + l;
                v = ((line[bx] >> bt) & 1) | (((line[8 + bx] >> bt) & 1) << 1);
                if (ol) v |= (((ol[bx] >> bt) & 1) << 2) | (((ol[8 + bx] >> bt) & 1) << 3); else if (v) v |= 12;
                if (v && X >= 0 && X < 320 && Y >= 0 && Y < 240) img[Y][X] = pal[16 + v];
            }
            d += 16 + (ve - vs) * 16; if (o) o += 16 + (ve - vs) * 16;
        }
    }
    f = fopen(name, "wb"); fprintf(f, "P6\n320 240\n255\n");
    for (y = 0; y < 240; y++) for (x = 0; x < 320; x++) { fputc(img[y][x] >> 16, f); fputc((img[y][x] >> 8) & 255, f); fputc(img[y][x] & 255, f); }
    fclose(f);
}

static const char *key0(unsigned t) { static char k[32]; snprintf(k, sizeof k, ",%u,", t); return k; }

int main(int argc, char **argv)
{
    unsigned n = atoi(argv[1]), t; char key[32], list[4096], name[64]; FILE *g;
    snprintf(list, sizeof list, ",%s,", argc > 2 ? argv[2] : "");
    g = fopen("assets/prg.bin", "rb"); if (!g || fread(nes_prg_data, 1, 0x20000, g) != 0x20000) return 2; fclose(g);
    g = fopen("assets/chr.bin", "rb"); if (!g || fread(nes_chr_data, 1, 0x20000, g) != 0x20000) return 2; fclose(g);
    game_init(); video_load(); disp_open(&video_display); video_start();
    for (t = 0; t < n; t++) {
        game_tick(game_bench_input(t));
        if (getenv("EVERY") && t % atoi(getenv("EVERY")) && !strstr(list, key0(t + 1))) continue;   /* draw only every Nth tick, like a slow Amiga */
        video_frame();
        snprintf(key, sizeof key, ",%u,", t + 1);
        if (strstr(list, key)) { snprintf(name, sizeof name, "vhost_%05u.ppm", t + 1); shot(name); }
    }
    printf("sprite clusters dropped: %d\n", video_sprites_dropped);
    return 0;
}
