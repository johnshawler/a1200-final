/* video.c - shows the NES picture on the AGA display (Amiga only). Reads the PPU state the game logic left in
 * src/nes.c once per frame and changes only what changed:
 *
 *  - Background: 4 bitplanes = the NES's 16 background colours (attribute bits are planes 2 and 3, so a
 *    palette change is only a colour register write). Hardware scrolling over an unwrapped world: NES tile
 *    columns are drawn into the buffer as the camera reaches them, a few per frame; the 30 tile rows are
 *    drawn twice (y and y + 240) so that the NES's vertical wrap is a plain scroll position. One buffer:
 *    nothing is drawn with the blitter, so nothing has to be erased.
 *  - Sprites: the NES sprites are grouped into clusters at most 64 pixels wide and drawn into 64-pixel
 *    hardware sprites, three attached pairs (15 colours: all four NES sprite palettes) and sprite 6 for
 *    clusters in NES sprite palette 3. Each hardware sprite is reused down the screen (several control
 *    words in one sprite list).
 *  - Raster split: this game only switches the tile set at its sprite-0 split (no scroll change), so tile
 *    rows above the split line are drawn with the upper tile set.
 */
#include <proto/exec.h>
#include <exec/memory.h>
#include <string.h>
#include "sys.h"
#include "disp.h"
#include "nes.h"
#include "video.h"

#define PLANES   4
#define LEVEL_W  4096                   /* unwrapped world width: the camera is re-centred if it gets near an end */
#define VIEW_X   32                     /* the 256-pixel NES picture starts 32 pixels into the 320-pixel view */
#define SPR_W    64

const DispCfg video_display = { PLANES, 1, 0, 240, 480, LEVEL_W, 1, 0, SPR_W, 1 };

extern const uint8_t nes_chr_data[];

static const ULONG nes_rgb[64] = {
0x666666,0x002a88,0x1412a7,0x3b00a4,0x5c007e,0x6e0040,0x6c0600,0x561d00,0x333500,0x0b4800,0x005200,0x004f08,0x00404d,0,0,0,
0xadadad,0x155fd9,0x4240ff,0x7527fe,0xa01acc,0xb71e7b,0xb53120,0x994e00,0x6b6d00,0x388700,0x0c9300,0x008f32,0x007c8d,0,0,0,
0xfffeff,0x64b0ff,0x9290ff,0xc676ff,0xf36aff,0xfe6ecc,0xfe8170,0xea9e22,0xbcbe00,0x88d800,0x5ce430,0x45e082,0x48cdde,0x4f4f4f,0,0,
0xfffeff,0xc0dfff,0xd3d2ff,0xe8c8ff,0xfbc2ff,0xfec4ea,0xfeccc5,0xf7d8a5,0xe4e594,0xcfef96,0xbdf4ab,0xb3f3cc,0xb5ebf2,0xb8b8b8,0,0};

/* ---------------------------------------------------------------- background */
static LONG cam_ux;                     /* unwrapped world x of NES screen x = 0 */
static WORD nes_sx_prev = -1;
static LONG col_lo, col_hi;             /* unwrapped tile columns col_lo..col_hi are drawn (col_lo > col_hi: none) */
static LONG work_col; static WORD work_row = -1;
static UBYTE drawn_bgsel, drawn_up0, drawn_up1, drawn_lo0, drawn_lo1, drawn_split;  /* the tile set the buffer holds */
static UBYTE flip_tab[256];

/* tile set for tile row r: upper part above the split line, lower part below */
static UBYTE split_row;                 /* first tile row drawn with the lower tile set (30 = no split) */
static UBYTE up0, up1, lo0, lo1, bgsel;

static const UBYTE *chr_tile(UBYTE tile, int row)
{
    UWORD a = (UWORD)((bgsel ? 0x1000 : 0) + tile * 16);
    UBYTE b0 = row < split_row ? up0 : lo0, b1 = row < split_row ? up1 : lo1, bank;
    if (mmc_ctrl & 0x10) bank = (a < 0x1000) ? b0 : b1;
    else bank = (UBYTE)((b0 & 0x1e) | (a >> 12));
    return nes_chr_data + (ULONG)(bank & 31) * 0x1000 + (a & 0xfff);
}

/* nametable cell of unwrapped column uc, row r (vertical mirroring: two nametables side by side) */
static UWORD nt_cell(LONG uc, int r) { return (UWORD)((((uc >> 5) & 1) << 10) | (r << 5) | (uc & 31)); }

static void draw_tile(LONG uc, int r)
{
    UWORD cell = nt_cell(uc, r), base = cell & 0x400;
    UBYTE tile = nes_vram[cell], tx = (UBYTE)(uc & 31);
    UBYTE attr = nes_vram[base | 0x3c0 | ((r >> 2) << 3) | (tx >> 2)];
    UBYTE pal = (UBYTE)((attr >> (((r & 2) << 1) | (tx & 2))) & 3);
    const UBYTE *src = chr_tile(tile, r);
    UBYTE p2 = (pal & 1) ? 0xff : 0, p3 = (pal & 2) ? 0xff : 0;
    LONG x = uc * 8;
    UBYTE *d;
    int i, copy;
    if (x < -64 || x >= LEVEL_W + 320) return;
    for (copy = 0; copy < 2; copy++) {
        d = disp_front + (ULONG)(r * 8 + copy * 240) * disp_row + ((x + disp_margin) >> 3);
        for (i = 0; i < 8; i++, d += disp_row) {
            UBYTE a = src[i], b = src[i + 8];
            d[0] = a;
            d[disp_plane] = b;
            d[2 * disp_plane] = p2;     /* colour 0 of every background palette is the same colour, */
            d[3 * disp_plane] = p3;     /* so the attribute planes need no pixel mask              */
        }
    }
}

/* the visible column range for the current camera, with a margin */
static void want_cols(LONG *lo, LONG *hi)
{
    *lo = (cam_ux - VIEW_X - 8) >> 3;
    *hi = (cam_ux - VIEW_X + 320 + 8) >> 3;
}

static void invalidate(void) { col_lo = 1; col_hi = 0; work_row = -1; }

/* draw up to n tiles of the columns that are needed but not drawn yet */
static void stream_cols(int n)
{
    LONG lo, hi;
    want_cols(&lo, &hi);
    if (col_lo > col_hi) { col_lo = (lo + hi) / 2; col_hi = col_lo - 1; work_row = -1; }
    if (col_lo < lo - 2) col_lo = lo - 2;                 /* forget columns far behind */
    if (col_hi > hi + 2) col_hi = hi + 2;
    while (n > 0) {
        if (work_row < 0) {
            if (col_hi < hi) work_col = col_hi + 1;
            else if (col_lo > lo) work_col = col_lo - 1;
            else return;
            work_row = 0;
        }
        draw_tile(work_col, work_row);
        n--;
        if (++work_row == 30) {
            if (work_col > col_hi) col_hi = work_col; else col_lo = work_col;
            work_row = -1;
        }
    }
}

/* the NES changed nametable cell i: redraw it where it is on screen (or the 16 tiles of an attribute cell) */
static int redraw_cell(UWORD i)
{
    LONG uc0 = col_lo, uc;
    int r, c, cnt = 0;
    if ((i & 0x3ff) < 0x3c0) {
        int cx = i & 31, row = (i >> 5) & 31, ntx = (i >> 10) & 1;
        for (uc = uc0 + ((((ntx << 5) | cx) - uc0) & 63); uc <= col_hi; uc += 64) { draw_tile(uc, row); cnt++; }
        if (work_row > row) { uc = work_col; if ((uc & 63) == ((ntx << 5) | cx)) { draw_tile(uc, row); cnt++; } }
    } else {
        int a = i & 0x3f, ntx = (i >> 10) & 1, ax = (a & 7) * 4, ay = (a >> 3) * 4;
        for (c = 0; c < 4; c++)
            for (uc = uc0 + ((((ntx << 5) | (ax + c)) - uc0) & 63); uc <= col_hi; uc += 64)
                for (r = ay; r < ay + 4 && r < 30; r++) { draw_tile(uc, r); cnt++; }
    }
    return cnt;
}

/* ---------------------------------------------------------------- sprites */
#define NSEG    24
#define MAXIN   64
typedef struct { WORD x0, x1, y0, y1; UBYTE n, pal3only; UBYTE idx[MAXIN]; } Group;
static Group grp[64];
static int ngrp;
typedef struct { Group *seg[NSEG]; int n; } Chan;
static Chan chan[4];                    /* 0..2: attached pairs (sprites 0/1, 2/3, 4/5), 3: sprite 6 */
#define SPR_BYTES (16 * (240 + 2 * NSEG + 4))
static UBYTE *spr_mem[2][7];            /* two sets of sprite lists, swapped every frame */
static int spr_set;
static UBYTE pal_lut[4][256];           /* palette bits as plane masks */
int video_sprites_dropped;

static void build_groups(void)
{
    int i, j, k;
    ngrp = 0;
    for (i = 0; i < 64; i++) {
        UBYTE y = nes_oam[i * 4], x = nes_oam[i * 4 + 3];
        WORD sy = (WORD)y + 1;
        if (y >= 0xef) continue;
        for (j = 0; j < ngrp; j++) {
            Group *g = &grp[j];
            WORD nx0 = x < g->x0 ? x : g->x0, nx1 = x + 8 > g->x1 ? x + 8 : g->x1;
            if (nx1 - nx0 > SPR_W || g->n >= MAXIN) continue;
            if (sy > g->y1 + 1 || sy + 8 < g->y0 - 1 || x > g->x1 + 1 || x + 8 < g->x0 - 1) continue;
            g->x0 = nx0; g->x1 = nx1;
            if (sy < g->y0) g->y0 = sy;
            if (sy + 8 > g->y1) g->y1 = sy + 8;
            g->idx[g->n++] = (UBYTE)i;
            if ((nes_oam[i * 4 + 2] & 3) != 3) g->pal3only = 0;
            break;
        }
        if (j == ngrp) {
            Group *g = &grp[ngrp++];
            g->x0 = x; g->x1 = x + 8; g->y0 = sy; g->y1 = sy + 8; g->n = 1; g->idx[0] = (UBYTE)i;
            g->pal3only = (nes_oam[i * 4 + 2] & 3) == 3;
        }
    }
    /* merge groups that touch, while they fit */
    for (i = 0; i < ngrp; i++)
        for (j = i + 1; j < ngrp; j++) {
            Group *a = &grp[i], *b = &grp[j];
            WORD nx0 = a->x0 < b->x0 ? a->x0 : b->x0, nx1 = a->x1 > b->x1 ? a->x1 : b->x1;
            if (nx1 - nx0 > SPR_W || a->n + b->n > MAXIN) continue;
            if (b->y0 > a->y1 + 1 || b->y1 < a->y0 - 1 || b->x0 > a->x1 + 1 || b->x1 < a->x0 - 1) continue;
            a->x0 = nx0; a->x1 = nx1;
            if (b->y0 < a->y0) a->y0 = b->y0;
            if (b->y1 > a->y1) a->y1 = b->y1;
            for (k = 0; k < b->n; k++) a->idx[a->n++] = b->idx[k];
            a->pal3only &= b->pal3only;
            grp[j] = grp[--ngrp]; j = i;            /* start again for this group */
        }
}

static int try_place(Chan *c, Group *g)
{
    if (c->n >= NSEG) return 0;
    if (c->n && c->seg[c->n - 1]->y1 + 1 > g->y0) return 0;
    c->seg[c->n++] = g;
    return 1;
}

static void assign(void)
{
    int i, j, k;
    Group *order[64];
    for (i = 0; i < 4; i++) chan[i].n = 0;
    for (i = 0; i < ngrp; i++) order[i] = &grp[i];
    for (i = 1; i < ngrp; i++) {                      /* by top line */
        Group *t = order[i];
        for (j = i; j > 0 && order[j - 1]->y0 > t->y0; j--) order[j] = order[j - 1];
        order[j] = t;
    }
    for (i = 0; i < ngrp; i++) {
        Group *g = order[i];
        if (g->pal3only && try_place(&chan[3], g)) continue;
        for (k = 0; k < 3; k++) if (try_place(&chan[k], g)) break;
        if (k == 3) video_sprites_dropped++;
    }
}

/* draw the NES sprites of group g into one segment's lines: e = even sprite lines, o = odd sprite lines */
static void render_group(Group *g, UBYTE *e, UBYTE *o)
{
    int k, r;
    for (k = g->n - 1; k >= 0; k--) {           /* highest OAM index first: lower numbers end on top */
        int i = g->idx[k];
        UBYTE tile = nes_oam[i * 4 + 1], at = nes_oam[i * 4 + 2];
        int dx = nes_oam[i * 4 + 3] - g->x0, dy = nes_oam[i * 4] + 1 - g->y0, sh = 8 - (dx & 7);
        int upper = split_on && (int)nes_oam[i * 4] + 1 <= split_line;      /* sprites above the split: upper tile set */
        UBYTE c0 = upper ? split_chr0 : mmc_chr0, c1 = upper ? split_chr1 : mmc_chr1, ctl = upper ? split_ctrl : ppu_ctrl;
        UWORD a = (UWORD)(((ctl & 0x08) ? 0x1000 : 0) + tile * 16), bank;
        const UBYTE *src, *pl2 = pal_lut[(at & 1) ? 1 : 0], *pl3 = pal_lut[(at & 2) ? 1 : 0];
        bank = (mmc_ctrl & 0x10) ? ((a < 0x1000) ? c0 : c1) : ((c0 & 0x1e) | (a >> 12));
        src = nes_chr_data + (ULONG)(bank & 31) * 0x1000 + (a & 0xfff);
        for (r = 0; r < 8; r++) {
            int row = (at & 0x80) ? 7 - r : r, line = dy + r, off;
            UBYTE p0 = src[row], p1 = src[row + 8], m;
            UWORD M, *w;
            if (at & 0x40) { p0 = flip_tab[p0]; p1 = flip_tab[p1]; }
            m = p0 | p1;
            if (!m) continue;
            M = (UWORD)(~(m << sh));
            off = line * 16 + (dx >> 3);
            w = (UWORD *)(e + off);     *w = (UWORD)((*w & M) | (p0 << sh));
            w = (UWORD *)(e + off + 8); *w = (UWORD)((*w & M) | (p1 << sh));
            if (o) {
                w = (UWORD *)(o + off);     *w = (UWORD)((*w & M) | (pl2[m] << sh));
                w = (UWORD *)(o + off + 8); *w = (UWORD)((*w & M) | (pl3[m] << sh));
            }
        }
    }
}

static void build_sprites(void)
{
    int k, s, n;
    spr_set ^= 1;
    build_groups();
    assign();
    for (k = 0; k < 4; k++) {
        Chan *c = &chan[k];
        int ne = k < 3 ? k * 2 : 6, no = k < 3 ? k * 2 + 1 : -1;
        UBYTE *e = spr_mem[spr_set][ne], *o = no >= 0 ? spr_mem[spr_set][no] : 0;
        if (!c->n) { disp_sprite_off((UWORD)ne); if (no >= 0) disp_sprite_off((UWORD)no); continue; }
        for (s = 0; s < c->n; s++) {
            Group *g = c->seg[s];
            WORD x = (WORD)(g->x0 + VIEW_X), y = g->y0, h = (WORD)(g->y1 - g->y0);
            UWORD hs = (UWORD)(x + 0x80), vs = (UWORD)(y + 0x34), ve = (UWORD)(vs + h);
            n = h * 16;
            if (s == 0) {                       /* the first control words: the kit writes them */
                disp_sprite((UWORD)ne, (UWORD *)e, x, y, (UWORD)h, 0);
                if (o) disp_sprite((UWORD)no, (UWORD *)o, x, y, (UWORD)h, 1);
            } else {
                ((UWORD *)e)[0] = (UWORD)(((vs & 0xff) << 8) | ((hs >> 1) & 0xff));
                ((UWORD *)e)[4] = (UWORD)(((ve & 0xff) << 8) | (((vs >> 8) & 1) << 2) | (((ve >> 8) & 1) << 1) | (hs & 1));
                if (o) { ((UWORD *)o)[0] = ((UWORD *)e)[0]; ((UWORD *)o)[4] = (UWORD)(((UWORD *)e)[4] | 0x80); }
            }
            memset(e + 16, 0, n);
            if (o) memset(o + 16, 0, n);
            render_group(g, e + 16, o ? o + 16 : 0);
            e += 16 + n;
            if (o) o += 16 + n;
        }
        memset(e, 0, 16);                       /* end of the list */
        if (o) memset(o, 0, 16);
    }
}

/* ---------------------------------------------------------------- frame */
static ULONG pal_rgb[32];
static UBYTE was_on;

static void set_palette(int on)
{
    int i;
    for (i = 0; i < 32; i++) {
        UBYTE c = (i & 3) ? nes_pal[i] : nes_pal[0];
        pal_rgb[i] = on ? nes_rgb[c & 0x3f] : 0;
    }
    disp_palette(pal_rgb, 0, 32);
}

int video_load(void)
{
    int i, j;
    for (i = 0; i < 256; i++) { UBYTE r = 0; for (j = 0; j < 8; j++) if (i & (1 << j)) r |= (UBYTE)(0x80 >> j); flip_tab[i] = r; }
    for (i = 0; i < 256; i++) { pal_lut[0][i] = 0; pal_lut[1][i] = (UBYTE)i; }
    for (i = 0; i < 2; i++)
        for (j = 0; j < 7; j++)
            if (!(spr_mem[i][j] = sys_alloc(SPR_BYTES, MEMF_CHIP | MEMF_CLEAR))) return 0;
    invalidate();
    return 1;
}

void video_start(void)
{
    cam_ux = LEVEL_W / 2;
    nes_sx_prev = -1;
    invalidate();
}

void video_frame(void)
{
    int on = (ppu_mask & 0x18) != 0, budget = on ? 80 : 400, i;
    WORD sx = (WORD)((((ppu_t & 0x1f) << 3) | ppu_fx) + ((ppu_t >> 10) & 1) * 256);
    WORD sy = (WORD)((((ppu_t >> 5) & 0x1f) * 8 + ((ppu_t >> 12) & 7)) % 240);
    UBYTE nsplit;

    /* camera: follow the NES scroll by its change since the last frame (it wraps at 512) */
    if (nes_sx_prev >= 0) {
        WORD d = (WORD)((sx - nes_sx_prev) & 511);
        if (d >= 256) d -= 512;
        cam_ux += d;
    } else cam_ux = LEVEL_W / 2 + sx;
    nes_sx_prev = sx;
    if (cam_ux < 256 || cam_ux > LEVEL_W - 512) { cam_ux = LEVEL_W / 2 + sx; invalidate(); }

    /* which tile set each row uses; a change means everything is drawn again */
    bgsel = (ppu_ctrl & 0x10) != 0;
    if (split_on) {
        nsplit = (UBYTE)((split_line + 1 + 7) >> 3);
        up0 = split_chr0; up1 = split_chr1; lo0 = mmc_chr0; lo1 = mmc_chr1;
        bgsel = (split_ctrl & 0x10) != 0;
    } else {
        nsplit = 30; up0 = lo0 = mmc_chr0; up1 = lo1 = mmc_chr1;
    }
    split_row = nsplit;
    if (nes_redraw_all || bgsel != drawn_bgsel || up0 != drawn_up0 || up1 != drawn_up1 || lo0 != drawn_lo0
        || lo1 != drawn_lo1 || nsplit != drawn_split) {
        drawn_bgsel = bgsel; drawn_up0 = up0; drawn_up1 = up1; drawn_lo0 = lo0; drawn_lo1 = lo1; drawn_split = nsplit;
        nes_redraw_all = 0;
        invalidate();
    }

    /* nametable cells the game wrote (a whole new screen: draw it again column by column) */
    if (nes_ndirty > 200) invalidate();
    else if (col_lo <= col_hi)
        for (i = 0; i < nes_ndirty; i++) budget -= redraw_cell(nes_dirty[i]);
    for (i = 0; i < nes_ndirty; i++) nes_dirty_flag[nes_dirty[i]] = 0;
    nes_ndirty = 0;
    if (budget < 8) budget = 8;
    stream_cols(budget);

    if (nes_pal_dirty || on != was_on) { set_palette(on); nes_pal_dirty = 0; was_on = (UBYTE)on; }
    disp_scroll(cam_ux - VIEW_X, sy);
    if (ppu_mask & 0x10) build_sprites();
    else for (i = 0; i < 7; i++) disp_sprite_off((UWORD)i);
}
