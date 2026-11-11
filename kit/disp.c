/* disp.c - AGA display for games: fixed or hardware-scrolling playfield, tiles, blitter objects, sprites. */
#include <proto/exec.h>
#include <exec/memory.h>
#include "sys.h"
#include "disp.h"

UWORD  disp_row, disp_margin;
ULONG  disp_plane;
UBYTE *disp_front, *disp_back, *disp_clean;

#define MAXRECT 48
typedef struct { LONG off; UWORD words, h; } Rect;       /* an area to erase: byte offset in plane 0, size */

static DispCfg cfg;
static UBYTE *mem[3];
static ULONG  memsize;
static UWORD *cop, *cop_con1, *cop_bpl, *cop_spr;
static ULONG *nullspr;
static UWORD  unit, fetched, diw_top, back_ix, nspr;
static LONG   scroll_x, scroll_y;
static Rect   rects[2][MAXRECT];
static UWORD  nrect[2];
static UWORD *spr_data[8], spr_pos[8], spr_ctl[8], spr_new;

static UWORD *cmove(UWORD *p, UWORD reg, UWORD val) { p[0] = reg; p[1] = val; return p + 2; }

void blit_wait(void)
{
    UWORD t = custom->dmaconr; (void)t;         /* the busy flag is only valid after one dummy read */
    while (custom->dmaconr & DMAF_BLTDONE) ;
}

int disp_open(const DispCfg *c)
{
    UWORD *p, i, sprbits, stop, mod;
    ULONG slide = 0;

    cfg = *c;
    unit = cfg.fetch == 3 ? 64 : cfg.fetch == 0 ? 16 : 32;      /* pixels per bitplane fetch */
    if (cfg.scroll) {
        disp_margin = 64;
        disp_row = 64;                                          /* 512 pixels: view + hidden fetch + slack */
        fetched = (320 / unit + 1) * (unit / 8);                /* 48, 44 or 42 bytes per line */
        slide = (cfg.level_w + 7) / 8 + 64;                     /* the picture slides through memory as x grows */
    } else {
        disp_margin = 0;
        disp_row = 40;
        fetched = 40;
    }
    disp_plane = ((ULONG)disp_row * cfg.buf_h + slide + 7) & ~7UL;
    memsize = disp_plane * cfg.planes;

    /* AllocMem() blocks are 8-byte aligned, as 64-bit fetch requires. */
    for (i = 0; i < cfg.buffers; i++)
    {   UBYTE *m = AllocMem(memsize + 16, MEMF_CHIP | MEMF_CLEAR);     /* 8 guard bytes on each side: clipped blits rewrite one word there */
        if (!m) { disp_close(); return 0; }
        mem[i] = m + 8;
    }
    cop = AllocMem(512, MEMF_CHIP | MEMF_CLEAR);
    nullspr = AllocMem(32, MEMF_CHIP | MEMF_CLEAR);
    if (!cop || !nullspr) { disp_close(); return 0; }
    disp_front = mem[0];
    disp_back  = mem[cfg.buffers >= 2 ? 1 : 0];
    disp_clean = cfg.buffers >= 3 ? mem[2] : 0;
    back_ix = cfg.buffers >= 2 ? 1 : 0;
    nrect[0] = nrect[1] = 0; spr_new = 0; scroll_x = scroll_y = 0;

    nspr = !cfg.scroll ? 8 : unit == 64 ? 1 : unit == 32 ? 5 : 7;      /* sprites that still get their DMA slots */
    sprbits = cfg.sprite_w == 64 ? 3 : cfg.sprite_w == 32 ? 1 : 0;
    diw_top = 0x2c + (256 - cfg.view_h) / 2;
    stop = diw_top + cfg.view_h;
    mod = disp_row - fetched;

    p = cop;
    p = cmove(p, 0x1fc, cfg.fetch | (sprbits << 2) | (cfg.double_y ? 0x4000 : 0));   /* FMODE */
    p = cmove(p, 0x100, 0x0201 | ((cfg.planes & 7) << 12) | ((cfg.planes & 8) << 1)); /* BPLCON0 */
    p = cmove(p, 0x104, 0x0224);                                 /* BPLCON2: sprites in front, 64 real colours at 6 planes */
    p = cmove(p, 0x10c, (cfg.sprite_bank << 4) | cfg.sprite_bank); /* BPLCON4: sprite colour bank */
    p = cmove(p, 0x08e, (diw_top << 8) | 0x81);                  /* DIWSTRT */
    p = cmove(p, 0x090, ((stop & 0xff) << 8) | 0xc1);            /* DIWSTOP */
    p = cmove(p, 0x092, cfg.scroll ? 0x38 - unit / 2 : 0x38);    /* DDFSTRT: one fetch early when scrolling */
    p = cmove(p, 0x094, 0x38 + (320 / unit - 1) * (unit / 2));   /* DDFSTOP: $D0, $C8 or $B8 */
    p = cmove(p, 0x108, cfg.double_y ? (UWORD)-fetched : mod);   /* BPL1MOD (with double_y: repeat the line) */
    p = cmove(p, 0x10a, mod);                                    /* BPL2MOD */
    p = cmove(p, 0x1001, 0xfffe);                                /* wait for line 16: disp_flip() patches what follows */
    cop_con1 = p;
    p = cmove(p, 0x102, 0);                                      /* BPLCON1: fine scroll */
    cop_bpl = p;
    for (i = 0; i < cfg.planes; i++) { p = cmove(p, 0x0e0 + i * 4, 0); p = cmove(p, 0x0e2 + i * 4, 0); }
    cop_spr = p;                                                 /* sprite pointers: read here, before sprite DMA starts (line 25 PAL, 20 NTSC) */
    for (i = 0; i < 8; i++) {
        p = cmove(p, 0x120 + i * 4, (ULONG)nullspr >> 16);
        p = cmove(p, 0x122 + i * 4, (ULONG)nullspr & 0xffff);
    }
    p = cmove(p, 0xffff, 0xfffe);
    return 1;
}

void disp_close(void)
{
    int i;
    for (i = 0; i < 3; i++) if (mem[i]) { FreeMem(mem[i] - 8, memsize + 16); mem[i] = 0; }
    if (cop) { FreeMem(cop, 512); cop = 0; }
    if (nullspr) { FreeMem(nullspr, 32); nullspr = 0; }
}

/* Write scroll position, bitplane pointers and sprite positions into the copper list.
   Called in the first lines of a frame; the copper reads them at line 20. */
static void commit(void)
{
    ULONG off = (ULONG)scroll_y * disp_row, a;
    UWORD con1 = 0, i, d, *p = cop_bpl;

    if (cfg.scroll) {
        ULONG x = (ULONG)scroll_x;
        d = (unit - (x & (unit - 1))) & (unit - 1);              /* delay in pixels, 0 .. unit-1 */
        off += (64 - unit) / 8 + ((x + unit - 1) / unit) * (unit / 8);
        con1 = (d & 15) | ((d & 15) << 4) | (((d >> 4) & 3) << 10) | (((d >> 4) & 3) << 14);
    }
    cop_con1[1] = con1;
    for (i = 0; i < cfg.planes; i++, p += 4) {
        a = (ULONG)disp_front + i * disp_plane + off;
        p[1] = a >> 16; p[3] = a & 0xffff;
    }
    for (i = 0; i < 8; i++)
        if (spr_new & (1 << i)) {
            UWORD *s = spr_data[i];
            if (s) { s[0] = spr_pos[i]; s[cfg.sprite_w / 16] = spr_ctl[i]; } else s = (UWORD *)nullspr;
            cop_spr[i * 4 + 1] = (ULONG)s >> 16; cop_spr[i * 4 + 3] = (ULONG)s & 0xffff;
        }
    spr_new = 0;
}

void disp_on(void)
{
    custom->bplcon3 = 0x0c00;
    commit();
    custom->cop1lc = (ULONG)cop;
    sys_wait_frame();
    custom->dmacon = DMAF_SETCLR | DMAF_MASTER | DMAF_RASTER | DMAF_COPPER | DMAF_BLITTER | DMAF_BLITHOG | DMAF_SPRITE;
}

void disp_palette(const ULONG *rgb, UWORD first, UWORD count)
{
    UWORD i;
    for (i = first; i < first + count; i++) {
        ULONG c = *rgb++;
        UWORD bank = (i >> 5) << 13;
        custom->bplcon3 = bank | 0x0c00;                         /* high 4 bits of R, G, B */
        custom->color[i & 31] = ((c >> 12) & 0xf00) | ((c >> 8) & 0x0f0) | ((c >> 4) & 0x00f);
        custom->bplcon3 = bank | 0x0e00;                         /* LOCT: low 4 bits */
        custom->color[i & 31] = ((c >> 8) & 0xf00) | ((c >> 4) & 0x0f0) | (c & 0x00f);
    }
    custom->bplcon3 = 0x0c00;
}

void disp_scroll(LONG x, LONG y) { scroll_x = x; scroll_y = y; }

/* Erase the objects that were drawn on the back buffer the last time it was the back buffer. */
void disp_begin(void)
{
    Rect *r = rects[back_ix];
    UWORD n = nrect[back_ix], p;
    if (!disp_clean) return;
    for (; n; n--, r++) {
        UBYTE *s = disp_clean + r->off, *d = disp_back + r->off;
        blit_wait();
        custom->bltcon0 = 0x09f0; custom->bltcon1 = 0;           /* D = A */
        custom->bltafwm = 0xffff; custom->bltalwm = 0xffff;
        custom->bltamod = custom->bltdmod = disp_row - r->words * 2;
        for (p = 0; p < cfg.planes; p++, s += disp_plane, d += disp_plane) {
            blit_wait();
            custom->bltapt = s; custom->bltdpt = d;
            custom->bltsize = (r->h << 6) | r->words;
        }
    }
    nrect[back_ix] = 0;
}

void disp_flip(void)
{
    blit_wait();
    sys_wait_frame();
    if (cfg.buffers >= 2) { UBYTE *t = disp_front; disp_front = disp_back; disp_back = t; back_ix ^= 1; }
    commit();
}

void disp_tile(LONG x, LONG y, const UWORD *tile)
{
    ULONG off = (ULONG)y * disp_row + ((x + disp_margin) >> 3);
    UWORD b, p;
    if (y < 0 || y + 16 > cfg.buf_h || x + (LONG)disp_margin < 0
        || x > (cfg.scroll ? (LONG)cfg.level_w + 320 : 304)) return;      /* outside the buffer: draw nothing */
    blit_wait();
    custom->bltcon0 = 0x09f0; custom->bltcon1 = 0;               /* D = A */
    custom->bltafwm = 0xffff; custom->bltalwm = 0xffff;
    custom->bltamod = 0; custom->bltdmod = disp_row - 2;
    for (b = 0; b < cfg.buffers; b++) {
        UBYTE *d = mem[b] + off;
        const UWORD *s = tile;
        for (p = 0; p < cfg.planes; p++, d += disp_plane, s += 16) {
            blit_wait();
            custom->bltapt = (APTR)s; custom->bltdpt = d;
            custom->bltsize = (16 << 6) | 1;
        }
    }
}

/* Masked draw of one object into one buffer. Returns 0 if nothing was drawn. */
static int cookie(UBYTE *buf, LONG x, LONG y, const UWORD *image, const UWORD *mask, UWORD wwords, UWORD h, Rect *r)
{
    LONG bx = x + disp_margin, skip = 0, d0, k = 0, over;
    UWORD sh, p, rows = h, w = wwords + 1, fwm = 0xffff, lwm = 0x0000;
    LONG off;
    UBYTE *d;
    if (y < 0) { skip = -y; if (skip >= h) return 0; rows = h - skip; y = 0; }      /* clip at the top */
    if (y + rows > cfg.buf_h) { if (y >= cfg.buf_h) return 0; rows = cfg.buf_h - y; } /* and at the bottom */
    sh = bx & 15;
    d0 = bx >> 4;                                                /* first word written; -1 = the word left of the row */
    if (bx < 0) {                                                /* clip at the left edge */
        if (cfg.scroll || -bx >= 16 * (LONG)wwords) return 0;
        k = -bx >> 4;                                            /* source words that are completely outside */
        fwm = 0xffff >> (-bx & 15);                              /* the pixels of the first word that are outside */
        d0 += k; w -= k;
    }
    if (!cfg.scroll && d0 + w > (disp_row >> 1)) {               /* clip at the right edge (a scrolling row wraps on purpose) */
        over = d0 + w - (disp_row >> 1);
        if (over >= w) return 0;
        w -= over; lwm = 0xffff << sh;                           /* no extra word: cut what would be shifted out of the row */
    }
    off = y * disp_row + d0 * 2;                                 /* can be -2: the guard word in front of the buffer */
    d = buf + off;
    blit_wait();
    custom->bltcon0 = 0x0fca | (sh << 12);                       /* D = mask ? image : background */
    custom->bltcon1 = sh << 12;
    custom->bltafwm = fwm; custom->bltalwm = lwm;
    custom->bltamod = custom->bltbmod = (wwords - w) * 2;        /* -2 when nothing is clipped */
    custom->bltcmod = custom->bltdmod = disp_row - w * 2;
    for (p = 0; p < cfg.planes; p++, d += disp_plane) {
        blit_wait();
        custom->bltapt = (APTR)(mask + skip * wwords + k);
        custom->bltbpt = (APTR)(image + ((ULONG)p * h + skip) * wwords + k);
        custom->bltcpt = d; custom->bltdpt = d;
        custom->bltsize = (rows << 6) | w;
    }
    if (r) { r->off = off; r->words = w; r->h = rows; }
    return 1;
}

void disp_bob(LONG x, LONG y, const UWORD *image, const UWORD *mask, UWORD wwords, UWORD h)
{
    Rect r;
    if (disp_clean && nrect[back_ix] >= MAXRECT) return;         /* erase list full: do not draw what could not be erased */
    if (!cookie(disp_back, x, y, image, mask, wwords, h, &r)) return;
    if (disp_clean) rects[back_ix][nrect[back_ix]++] = r;
}

void disp_stamp(LONG x, LONG y, const UWORD *image, const UWORD *mask, UWORD wwords, UWORD h)
{
    UWORD b;
    for (b = 0; b < cfg.buffers; b++) cookie(mem[b], x, y, image, mask, wwords, h, 0);
}

void disp_paste(const UBYTE *picture, LONG x, LONG y, UWORD w, UWORD h)
{
    ULONG off = (ULONG)y * disp_row + (x >> 3);
    UWORD b, p;
    if (cfg.scroll || x < 0 || y < 0 || !w || !h || x + w > 320 || y + h > cfg.buf_h) return;   /* fixed screens only, inside the picture */
    blit_wait();
    custom->bltcon0 = 0x09f0; custom->bltcon1 = 0;               /* D = A */
    custom->bltafwm = 0xffff; custom->bltalwm = 0xffff;
    custom->bltamod = custom->bltdmod = disp_row - (w >> 3);
    for (b = 0; b < cfg.buffers; b++) {
        const UBYTE *s = picture + off;
        UBYTE *d = mem[b] + off;
        for (p = 0; p < cfg.planes; p++, s += disp_plane, d += disp_plane) {
            blit_wait();
            custom->bltapt = (APTR)s; custom->bltdpt = d;
            custom->bltsize = (h << 6) | (w >> 4);
        }
    }
}

void disp_clear(UBYTE *buf, LONG x, LONG y, UWORD w, UWORD h)
{
    UBYTE *d = buf + (ULONG)y * disp_row + ((x + disp_margin) >> 3);
    UWORD p;
    if (y < 0 || !w || !h || y + h > cfg.buf_h || x + (LONG)disp_margin < 0
        || x + w > (cfg.scroll ? (LONG)cfg.level_w + 320 : 320)) return;   /* outside the buffer: clear nothing */
    blit_wait();
    custom->bltcon0 = 0x0100; custom->bltcon1 = 0;               /* D = 0 */
    custom->bltdmod = disp_row - (w >> 3);
    for (p = 0; p < cfg.planes; p++, d += disp_plane) {
        blit_wait();
        custom->bltdpt = d;
        custom->bltsize = (h << 6) | (w >> 4);
    }
}

void disp_sprite(UWORD n, UWORD *data, WORD x, WORD y, UWORD h, UWORD attach)
{
    UWORD hs = x + 0x80, vs = y + diw_top, ve = vs + h;
    if (n >= nspr) return;                                       /* this sprite number has no DMA slots on a scrolling screen */
    if (x <= -(WORD)cfg.sprite_w || x >= 320 || y <= -(WORD)h || y >= (WORD)cfg.view_h
        || y + (WORD)diw_top < (sys_is_pal ? 26 : 21)) { disp_sprite_off(n); return; }   /* sprites cannot start during vertical blank */
    spr_data[n] = data;
    spr_pos[n] = (UWORD)((vs & 0xff) << 8) | ((hs >> 1) & 0xff);
    spr_ctl[n] = (UWORD)((ve & 0xff) << 8) | (attach ? 0x80 : 0) | (((vs >> 8) & 1) << 2) | (((ve >> 8) & 1) << 1) | (hs & 1);
    spr_new |= 1 << n;
}

void disp_sprite_off(UWORD n)
{
    spr_data[n] = 0;
    spr_new |= 1 << n;
}
