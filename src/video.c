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

/* 16-bit big-endian access: a plain word access on the Amiga; byte by byte for the host harness tools/vhost.c */
#ifdef __m68k__
#define GETW(p)    (*(UWORD *)(p))
#define PUTW(p, v) (*(UWORD *)(p) = (UWORD)(v))
#else
#define GETW(p)    ((UWORD)(((UBYTE *)(p))[0] << 8 | ((UBYTE *)(p))[1]))
#define PUTW(p, v) (((UBYTE *)(p))[0] = (UBYTE)((v) >> 8), ((UBYTE *)(p))[1] = (UBYTE)(v))
#endif

#ifdef LOGIC_TRACE
ULONG vprof[4], vtiles;                 /* debug: colour clocks in cell redraws, streaming, sprites; tiles drawn */
extern uint32_t logic_frames;
#define VP(i, x) do { ULONG t0_ = sys_now(); x; if (logic_frames >= 2000 && logic_frames < 2500) vprof[i] += sys_now() - t0_; } while (0)
#else
#define VP(i, x) do { x; } while (0)
#endif

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
UBYTE video_flip[256];                  /* bit-reversed bytes, for h-flipped sprites */
ULONG video_plane;                      /* bytes from one bitplane to the next (src/video68k.s) */
#define flip_tab video_flip
#ifdef __m68k__
void tile8(UBYTE *d __asm("a0"), const UBYTE *chr __asm("a1"), ULONG attr __asm("d0"));
void spr_put(UBYTE *e __asm("a0"), UBYTE *o __asm("a1"), const UBYTE *chr __asm("a2"), ULONG ctl __asm("d0"));
#else                                   /* the same in C, for the host harness tools/vhost.c */
static void tile8(UBYTE *d, const UBYTE *chr, ULONG attr)
{
    int i;
    for (i = 0; i < 8; i++, d += 64) { d[0] = chr[i]; d[video_plane] = chr[i + 8]; d[2 * video_plane] = (UBYTE)attr; d[3 * video_plane] = (UBYTE)(attr >> 8); }
}
static void spr_put(UBYTE *e, UBYTE *o, const UBYTE *chr, ULONG ctl)
{
    int dx = ctl & 63, sh = 8 - (dx & 7), r;
    e += dx >> 3; o += dx >> 3;
    for (r = 0; r < 8; r++, e += 16, o += 16) {
        int row = (ctl & 0x200) ? 7 - r : r;
        UBYTE p0 = chr[row], p1 = chr[row + 8];
        UWORD m, k;
        if (ctl & 0x100) { p0 = video_flip[p0]; p1 = video_flip[p1]; }
        if (!(p0 | p1)) continue;
        m = (UWORD)((p0 | p1) << sh); k = (UWORD)~m;
        PUTW(e, (GETW(e) & k) | (p0 << sh)); PUTW(e + 8, (GETW(e + 8) & k) | (p1 << sh));
        if (ctl & 0x1000) { PUTW(o, (GETW(o) & k) | ((ctl & 0x400) ? m : 0)); PUTW(o + 8, (GETW(o + 8) & k) | ((ctl & 0x800) ? m : 0)); }
    }
}
#endif

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

/* Columns in the 32-pixel borders beside the 256-pixel NES picture are drawn in the backdrop colour: the NES
   never shows them, and their nametable content belongs to the other side of the wrap. */
static UBYTE colstate[64];              /* by unwrapped column & 63: 1 drawn from the nametable, 2 drawn blank */
/* the NES hides its leftmost 8 pixels when PPUMASK bit 1 is clear: the game streams new columns there */
static int col_visible(LONG uc) { LONG x = uc * 8; return x + 8 > cam_ux + ((ppu_mask & 2) ? 0 : 8) && x < cam_ux + 256; }

/* Each tile row is in the buffer twice (y and y + 240) so that any vertical scroll shows 240 contiguous lines,
   but only the copies the current scroll shows are kept up to date: rowcopies[r] bit 0 = the copy at y,
   bit 1 = the copy at y + 240. When the scroll brings the other copy into view, that row is drawn again. */
static UBYTE rowcopies[30];
static UBYTE row_need(int r, int sy) { return (UBYTE)((8 * r + 8 > sy ? 1 : 0) | (8 * r < sy ? 2 : 0)); }

static void draw_tile_copies(LONG uc, int r, UBYTE copies);
static void draw_tile(LONG uc, int r) { draw_tile_copies(uc, r, rowcopies[r]); }

static void draw_tile_copies(LONG uc, int r, UBYTE copies)
{
    UWORD cell = nt_cell(uc, r), base = cell & 0x400;
    UBYTE tile = nes_vram[cell], tx = (UBYTE)(uc & 31);
    UBYTE attr = nes_vram[base | 0x3c0 | ((r >> 2) << 3) | (tx >> 2)];
    UBYTE pal = (UBYTE)((attr >> (((r & 2) << 1) | (tx & 2))) & 3);
    const UBYTE *src = chr_tile(tile, r);
    UBYTE p2 = (pal & 1) ? 0xff : 0, p3 = (pal & 2) ? 0xff : 0;
    LONG x = uc * 8;
    UBYTE *d;
    if (x < -64 || x >= LEVEL_W + 320) return;
    if (colstate[uc & 63] == 2) { static const UBYTE blank[16]; src = blank; p2 = p3 = 0; }   /* a border column */
#ifdef LOGIC_TRACE
    { extern ULONG vtiles; vtiles++; }
#endif
    d = disp_front + (ULONG)(r * 8) * 64 + ((x + disp_margin) >> 3);
    if (copies & 1) tile8(d, src, (ULONG)(p3 << 8 | p2));
    if (copies & 2) tile8(d + 240 * 64, src, (ULONG)(p3 << 8 | p2));     /* the copy for the vertical wrap */
}

/* the visible column range for the current camera, with a margin */
static void want_cols(LONG *lo, LONG *hi)
{
    *lo = (cam_ux - VIEW_X - 8) >> 3;
    *hi = (cam_ux - VIEW_X + 320 + 8) >> 3;
}

static void invalidate(void) { col_lo = 1; col_hi = 0; work_row = -1; }

/* make every row's copies match what the scroll position sy shows */
static void sync_rows(int sy)
{
    int r;
    LONG uc;
    for (r = 0; r < 30; r++) {
        UBYTE need = row_need(r, sy), missing = (UBYTE)(need & ~rowcopies[r]);
        if (missing && col_lo <= col_hi) {
            for (uc = col_lo; uc <= col_hi; uc++) draw_tile_copies(uc, r, missing);
            if (work_row > r) draw_tile_copies(work_col, r, missing);
        }
        rowcopies[r] = need;
    }
}

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
            colstate[work_col & 63] = col_visible(work_col) ? 1 : 2;
        }
        draw_tile(work_col, work_row);
        n--;
        if (++work_row == 30) {
            if (work_col > col_hi) col_hi = work_col; else col_lo = work_col;
            work_row = -1;
        }
    }
}

/* columns that crossed the edge of the NES picture since they were drawn */
static void fix_edges(void)
{
    LONG uc;
    int r;
    for (uc = col_lo; uc <= col_hi; uc++)            /* blank columns that came into the picture; columns that */
        if (colstate[uc & 63] == 2 && col_visible(uc)) { /* left it keep their (correct) content                */
            colstate[uc & 63] = 1;
            for (r = 0; r < 30; r++) draw_tile(uc, r);
        }
}

/* the NES changed nametable cell i: redraw it where it is on screen (or the 16 tiles of an attribute cell) */
static int redraw_cell(UWORD i)
{
    LONG uc0 = col_lo, uc;
    int r, c, cnt = 0;
    if ((i & 0x3ff) < 0x3c0) {
        int cx = i & 31, row = (i >> 5) & 31, ntx = (i >> 10) & 1;
        for (uc = uc0 + ((((ntx << 5) | cx) - uc0) & 63); uc <= col_hi; uc += 64)
            if (col_visible(uc)) { draw_tile(uc, row); cnt++; }       /* the border is not updated */
        if (work_row > row) { uc = work_col; if ((uc & 63) == ((ntx << 5) | cx) && col_visible(uc)) { draw_tile(uc, row); cnt++; } }
    } else {
        int a = i & 0x3f, ntx = (i >> 10) & 1, ax = (a & 7) * 4, ay = (a >> 3) * 4;
        for (c = 0; c < 4; c++)
            for (uc = uc0 + ((((ntx << 5) | (ax + c)) - uc0) & 63); uc <= col_hi; uc += 64)
                if (col_visible(uc))
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
int video_sprites_dropped;

static void build_groups(void)
{
    int i, j, k;
    ngrp = 0;
    for (i = 0; i < 64; i++) {
        UBYTE y = nes_oam[i * 4], x = nes_oam[i * 4 + 3];
        WORD sy = (WORD)y + 1;
        if (y >= 0xef) continue;
        for (j = ngrp > 4 ? ngrp - 4 : 0; j < ngrp; j++) {   /* a metasprite's sprites come together in OAM */
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
        if (j >= ngrp) {
            Group *g = &grp[ngrp++];
            g->x0 = x; g->x1 = x + 8; g->y0 = sy; g->y1 = sy + 8; g->n = 1; g->idx[0] = (UBYTE)i;
            g->pal3only = (nes_oam[i * 4 + 2] & 3) == 3;
        }
    }
    /* merge groups that touch, while they fit */
    for (i = 0; i < ngrp; i++)
        for (j = i + 1; j < ngrp && j <= i + 6; j++) {   /* neighbours in OAM order only */
            Group *a = &grp[i], *b = &grp[j];
            WORD nx0 = a->x0 < b->x0 ? a->x0 : b->x0, nx1 = a->x1 > b->x1 ? a->x1 : b->x1;
            if (nx1 - nx0 > SPR_W || a->n + b->n > MAXIN) continue;
            if (b->y0 > a->y1 + 1 || b->y1 < a->y0 - 1 || b->x0 > a->x1 + 1 || b->x1 < a->x0 - 1) continue;
            a->x0 = nx0; a->x1 = nx1;
            if (b->y0 < a->y0) a->y0 = b->y0;
            if (b->y1 > a->y1) a->y1 = b->y1;
            for (k = 0; k < b->n; k++) a->idx[a->n++] = b->idx[k];
            a->pal3only &= b->pal3only;
            grp[j] = grp[--ngrp]; j--;              /* look at the group moved into slot j */
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
    int k;
    for (k = g->n - 1; k >= 0; k--) {           /* highest OAM index first: lower numbers end on top */
        int i = g->idx[k];
        UBYTE tile = nes_oam[i * 4 + 1], at = nes_oam[i * 4 + 2];
        int dx = nes_oam[i * 4 + 3] - g->x0, dy = nes_oam[i * 4] + 1 - g->y0;
        int upper = split_on && (int)nes_oam[i * 4] + 1 <= split_line;      /* sprites above the split: upper tile set */
        UBYTE c0 = upper ? split_chr0 : mmc_chr0, c1 = upper ? split_chr1 : mmc_chr1, ctl = upper ? split_ctrl : ppu_ctrl;
        UWORD a = (UWORD)(((ctl & 0x08) ? 0x1000 : 0) + tile * 16), bank;
        const UBYTE *src;
        bank = (mmc_ctrl & 0x10) ? ((a < 0x1000) ? c0 : c1) : ((c0 & 0x1e) | (a >> 12));
        src = nes_chr_data + (ULONG)(bank & 31) * 0x1000 + (a & 0xfff);
        spr_put(e + dy * 16, o ? o + dy * 16 : e, src,
                (ULONG)(dx | ((at & 0x40) << 2) | ((at & 0x80) << 2) | ((at & 3) << 10) | (o ? 0x1000 : 0)));
    }
}

static void clear_lines(UBYTE *p, int n)
{
    ULONG *l = (ULONG *)p;
    for (n >>= 4; n; n--) { l[0] = 0; l[1] = 0; l[2] = 0; l[3] = 0; l += 4; }
}

static UBYTE scr_e[16 * 256], scr_o[16 * 256];    /* one segment is put together here (fast RAM if there is any) */

/* Segments drawn last frame, by content: a group with the same sprites, tiles, attributes and layout is copied
   from the previous set of sprite lists (which stays untouched while this set is built) instead of drawn. */
typedef struct { ULONG sig; UWORD n; UBYTE *e, *o; } Cached;
static Cached cache[2][4 * NSEG];
static int ncache[2];

static ULONG group_sig(Group *g, int with_o)
{
    ULONG h = (ULONG)g->n * 0x9e3779b1UL ^ (ULONG)(g->y1 - g->y0) ^ ((ULONG)with_o << 31);
    int k;
    for (k = 0; k < g->n; k++) {
        int i = g->idx[k];
        int upper = split_on && (int)nes_oam[i * 4] + 1 <= split_line;
        ULONG v = ((ULONG)nes_oam[i * 4 + 1] << 24) | ((ULONG)nes_oam[i * 4 + 2] << 16)
                | ((ULONG)(UBYTE)(nes_oam[i * 4 + 3] - g->x0) << 8) | (UBYTE)(nes_oam[i * 4] + 1 - g->y0);
        h = (h ^ v ^ (ULONG)upper) * 16777619UL + (ULONG)k;
    }
    return h;
}

static void copy_lines(UBYTE *d, const UBYTE *s, int n)
{
    ULONG *l = (ULONG *)d; const ULONG *m = (const ULONG *)s;
    for (n >>= 4; n; n--) { l[0] = m[0]; l[1] = m[1]; l[2] = m[2]; l[3] = m[3]; l += 4; m += 4; }
}

static void build_sprites(void)
{
    int k, s, n;
    spr_set ^= 1;
    ncache[spr_set] = 0;
    VP(3, { build_groups(); assign(); });
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
                PUTW(e, ((vs & 0xff) << 8) | ((hs >> 1) & 0xff));
                PUTW(e + 8, ((ve & 0xff) << 8) | (((vs >> 8) & 1) << 2) | (((ve >> 8) & 1) << 1) | (hs & 1));
                if (o) { PUTW(o, GETW(e)); PUTW(o + 8, GETW(e + 8) | 0x80); }
            }
            {   ULONG sig = group_sig(g, o != 0);
                Cached *cp = cache[spr_set ^ 1], *hit = 0;
                int j;
                for (j = 0; j < ncache[spr_set ^ 1]; j++) if (cp[j].sig == sig && cp[j].n == n) { hit = &cp[j]; break; }
                if (hit) {
                    copy_lines(e + 16, hit->e, n);
                    if (o) copy_lines(o + 16, hit->o, n);
                } else {
                    clear_lines(scr_e, n);
                    if (o) clear_lines(scr_o, n);
                    render_group(g, scr_e, o ? scr_o : 0);
                    copy_lines(e + 16, scr_e, n);       /* chip RAM is only written, once, a long at a time */
                    if (o) copy_lines(o + 16, scr_o, n);
                }
                if (ncache[spr_set] < 4 * NSEG) {
                    Cached *c2 = &cache[spr_set][ncache[spr_set]++];
                    c2->sig = sig; c2->n = (UWORD)n; c2->e = e + 16; c2->o = o ? o + 16 : 0;
                }
            }
            e += 16 + n;
            if (o) o += 16 + n;
        }
        memset(e, 0, 16);                       /* end of the list */
        if (o) memset(o, 0, 16);
    }
}

/* ---------------------------------------------------------------- frame */
static ULONG pal_rgb[32];
static UBYTE spr_valid;
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
    for (i = 0; i < 2; i++)
        for (j = 0; j < 7; j++)
            if (!(spr_mem[i][j] = sys_alloc(SPR_BYTES, MEMF_CHIP | MEMF_CLEAR))) return 0;
    video_plane = 0;
    invalidate();
    return 1;
}

void video_start(void)
{
    video_plane = disp_plane;
    cam_ux = LEVEL_W / 2;
    nes_sx_prev = -1;
    invalidate();
}


void video_frame(void)
{
    int on = (ppu_mask & 0x18) != 0, budget = 0, i;
    LONG vlo, vhi;
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

    VP(1, sync_rows(sy));

    /* nametable cells the game wrote (a whole new screen: draw it again column by column) */
    if (nes_ndirty > 200) invalidate();
    else if (col_lo <= col_hi)
        VP(0, for (i = 0; i < nes_ndirty; i++) budget -= redraw_cell(nes_dirty[i]));
    for (i = 0; i < nes_ndirty; i++) nes_dirty_flag[nes_dirty[i]] = 0;
    nes_ndirty = 0;
    /* while the visible part is incomplete (a new screen), show black and draw quickly, as the NES's own
       screen changes do; otherwise a few tiles a frame keep ahead of the scrolling */
    want_cols(&vlo, &vhi);
    budget = (on && col_lo <= vlo && col_hi >= vhi) ? 24 : 110;
    VP(1, stream_cols(budget));
    if (!(col_lo <= vlo && col_hi >= vhi)) on = 0;
    if (col_lo <= col_hi) fix_edges();

    if (nes_pal_dirty || on != was_on) { set_palette(on); nes_pal_dirty = 0; was_on = (UBYTE)on; }
    disp_scroll(cam_ux - VIEW_X, sy);
    if ((ppu_mask & 0x10) && on) {
        /* the sprite lists only change when the sprites or their tile set do */
        static UBYTE last_oam[256], last_state[8];
        UBYTE st[8];
        st[0] = ppu_ctrl; st[1] = mmc_chr0; st[2] = mmc_chr1; st[3] = split_on; st[4] = split_chr0; st[5] = split_chr1; st[6] = split_line; st[7] = mmc_ctrl;
        if (!spr_valid || memcmp(last_oam, nes_oam, 256) || memcmp(last_state, st, 8)) {
            VP(2, build_sprites());
            memcpy(last_oam, nes_oam, 256); memcpy(last_state, st, 8); spr_valid = 1;
        }
    }
    else { for (i = 0; i < 7; i++) disp_sprite_off((UWORD)i); spr_valid = 0; }
}

