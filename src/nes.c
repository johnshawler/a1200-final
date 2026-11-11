/* nes.c - NES memory map, MMC1 mapper and the PPU/APU/controller registers as plain state. See nes.h. */
#include <string.h>
#include "nes.h"

uint8_t cpu_a, cpu_x, cpu_y, cpu_s = 0xfd, cpu_c, cpu_z = 1, cpu_n, cpu_v, cpu_i = 1, cpu_d;
uint8_t nes_ram[0x800];
const uint8_t *nes_prg, *nes_chr, *nes_prg_lo, *nes_prg_hi;
uint8_t nes_bank;
uint16_t nes_fatal_pc; int nes_fatal_why;

uint8_t nes_vram[0x800], nes_pal[32], nes_oam[256];
uint8_t ppu_ctrl, ppu_mask, ppu_fx, ppu_vbl;
uint16_t ppu_t;
uint16_t ppu_v;                                 /* also used by the 68k fast path in src/nes68k.s */
static uint8_t ppu_w, ppu_oamaddr, ppu_rdbuf, ppu_s0;
uint8_t mmc_ctrl = 0x0c, mmc_chr0, mmc_chr1, mmc_prg;
uint8_t mmc_shift = 0x10;                       /* also used by the 68k fast paths in src/nes68k.s */

uint16_t nes_dirty[NES_DIRTY_MAX];
uint16_t nes_ndirty;
uint8_t  nes_dirty_flag[0x800];
uint8_t  nes_redraw_all = 1, nes_pal_dirty = 1;

uint8_t  split_on, split_ctrl, split_chr0, split_chr1, split_fx, split_line, split_seen;
uint16_t split_t;

uint8_t apu_reg[0x18], apu_trig;
uint8_t nes_pad;
uint8_t pad_shift, pad_strobe;                 /* also used by src/nes68k.s */

static void map_prg(void)
{
    int mode = (mmc_ctrl >> 2) & 3, b = mmc_prg & 0x0f;
    int lo, hi;
    if (mode < 2) { lo = b & 0x0e; hi = lo | 1; }
    else if (mode == 2) { lo = 0; hi = b; }
    else { lo = b; hi = 7; }
    lo &= 7; hi &= 7;
    nes_bank = (uint8_t)lo;
    nes_prg_lo = nes_prg + lo * 0x4000;
    nes_prg_hi = nes_prg + hi * 0x4000;
}

void nes_init(const uint8_t *prg, const uint8_t *chr)
{
    nes_prg = prg; nes_chr = chr;
    map_prg();
}

void nes_fatal(uint16_t pc, int why)
{
    if (!nes_fatal_why) { nes_fatal_pc = pc; nes_fatal_why = why; }
}

/* index into nes_vram for a PPU address $2000-$2FFF, after mirroring */
static uint16_t nt_index(uint16_t a)
{
    a &= 0xfff;
    switch (mmc_ctrl & 3) {
    case 0: return a & 0x3ff;
    case 1: return 0x400 | (a & 0x3ff);
    case 2: return a & 0x7ff;
    default: return ((a >> 1) & 0x400) | (a & 0x3ff);
    }
}

static void mark(uint16_t i)
{
    if (nes_dirty_flag[i]) return;
    if (nes_ndirty >= NES_DIRTY_MAX) { nes_redraw_all = 1; return; }
    nes_dirty_flag[i] = 1;
    nes_dirty[nes_ndirty++] = i;
}

static void vram_write(uint8_t v)
{
    uint16_t a = ppu_v & 0x3fff;
    if (a >= 0x3f00) {
        a &= 0x1f;
        if ((a & 0x13) == 0x10) a &= 0x0f;
        v &= 0x3f;
        if (nes_pal[a] != v) { nes_pal[a] = v; nes_pal_dirty = 1; }
    } else if (a >= 0x2000) {
        uint16_t i = nt_index(a);
        if (nes_vram[i] != v) { nes_vram[i] = v; mark(i); }
    }                                           /* CHR is ROM: writes below $2000 are ignored */
    ppu_v += (ppu_ctrl & 4) ? 32 : 1;
}

static uint8_t vram_read(void)
{
    uint16_t a = ppu_v & 0x3fff;
    uint8_t r = ppu_rdbuf, d;
    if (a < 0x2000) {
        uint16_t bank = (mmc_ctrl & 0x10) ? ((a < 0x1000) ? mmc_chr0 : mmc_chr1) : ((mmc_chr0 & 0x1e) | (a >> 12));
        d = nes_chr[(uint32_t)(bank & 31) * 0x1000 + (a & 0xfff)];
    } else d = nes_vram[nt_index(a)];
    if (a >= 0x3f00) { uint16_t p = a & 0x1f; if ((p & 0x13) == 0x10) p &= 0x0f; r = nes_pal[p]; }
    ppu_rdbuf = d;
    ppu_v += (ppu_ctrl & 4) ? 32 : 1;
    return r;
}

uint8_t nes_io_read(uint16_t a)
{
    if (a < 0x4000) {
        switch (a & 7) {
        case 2: { uint8_t r = (uint8_t)(ppu_vbl << 7); ppu_vbl = 0; ppu_w = 0; return r; }
        case 4: return nes_oam[ppu_oamaddr];
        case 7: return vram_read();
        }
        return 0;
    }
    if (a == 0x4016) {
        uint8_t r = pad_shift & 1;
        if (!pad_strobe) pad_shift = (uint8_t)((pad_shift >> 1) | 0x80);
        return r | 0x40;
    }
    if (a == 0x4017) return 0x40;
    return 0;                                   /* APU status, open bus, no PRG RAM on this board */
}

/* $2002 read inside a loop that waits for the sprite 0 hit. The hit flag toggles at every such read, so a
   loop waiting for it to clear and then to set ends after at most two reads; the set one marks the raster
   split: what the game changes after it applies to the lower part of the picture. */
uint8_t nes_read_s0(void)
{
    uint8_t r = (uint8_t)((ppu_vbl << 7) | (ppu_s0 << 6));
    if (ppu_s0) {
        split_seen = 1;
        split_ctrl = ppu_ctrl; split_chr0 = mmc_chr0; split_chr1 = mmc_chr1; split_t = ppu_t; split_fx = ppu_fx;
        split_line = nes_oam[0];
    }
    ppu_vbl = 0; ppu_w = 0; ppu_s0 ^= 1;
    return r;
}

/* the PRG register as five serial writes leave it (the 68k version of the game's bank switch calls this) */
void nes_mmc_prg(uint8_t v)
{
    mmc_prg = v;
    map_prg();
    mmc_shift = 0x10;
}

/* MMC1 register 0 (control) as five serial writes leave it */
void nes_mmc_ctrl(uint8_t v)
{
    if ((v ^ mmc_ctrl) & 0x03) nes_redraw_all = 1;
    mmc_ctrl = v;
    map_prg();
    mmc_shift = 0x10;
}

static void mmc1(uint16_t a, uint8_t v)
{
    if (v & 0x80) { mmc_shift = 0x10; mmc_ctrl |= 0x0c; map_prg(); return; }
    {
        int done = mmc_shift & 1;
        mmc_shift = (uint8_t)((mmc_shift >> 1) | ((v & 1) << 4));
        if (done) {
            uint8_t val = mmc_shift;
            switch ((a >> 13) & 3) {
            case 0: if ((val ^ mmc_ctrl) & 0x03) nes_redraw_all = 1; mmc_ctrl = val; break;   /* mirroring */
            case 1: mmc_chr0 = val; break;          /* tile sets: the display compares what it drew with */
            case 2: mmc_chr1 = val; break;
            default: mmc_prg = val; break;
            }
            map_prg();
            mmc_shift = 0x10;
        }
    }
}

void nes_write_slow(uint16_t a, uint8_t v)
{
    if (a < 0x4000) {
        switch (a & 7) {
        case 0: ppu_ctrl = v; ppu_t = (uint16_t)((ppu_t & 0xf3ff) | ((v & 3) << 10)); break;
        case 1: ppu_mask = v; break;
        case 3: ppu_oamaddr = v; break;
        case 4: nes_oam[ppu_oamaddr++] = v; break;
        case 5: if (!ppu_w) { ppu_t = (uint16_t)((ppu_t & 0xffe0) | (v >> 3)); ppu_fx = v & 7; }
                else ppu_t = (uint16_t)((ppu_t & 0x8c1f) | ((v & 7) << 12) | ((v & 0xf8) << 2));
                ppu_w ^= 1; break;
        case 6: if (!ppu_w) ppu_t = (uint16_t)((ppu_t & 0x00ff) | ((v & 0x3f) << 8));
                else { ppu_t = (uint16_t)((ppu_t & 0xff00) | v); ppu_v = ppu_t; }
                ppu_w ^= 1; break;
        case 7: vram_write(v); break;
        }
        return;
    }
    if (a < 0x4020) {
        if (a == 0x4014) {                      /* OAM DMA */
            uint16_t src = (uint16_t)(v << 8); int i;
            if (src < 0x2000 && ppu_oamaddr == 0)          /* the usual case: a whole RAM page */
                memcpy(nes_oam, nes_ram + (src & 0x7ff), 256);
            else
                for (i = 0; i < 256; i++) nes_oam[(uint8_t)(ppu_oamaddr + i)] = nes_rd((uint16_t)(src + i));
        } else if (a == 0x4016) {
            pad_strobe = v & 1;
            if (pad_strobe) pad_shift = nes_pad;
        } else if (a < 0x4018) {
            apu_reg[a - 0x4000] = v;
            if ((a & 3) == 3 && a < 0x4010) apu_trig |= (uint8_t)(1 << ((a - 0x4000) >> 2));
            if (a == 0x4015) apu_trig |= 0x10;
        }
        return;
    }
    if (a >= 0x8000) mmc1(a, v);                /* $4020-$7FFF: nothing there on this board */
}
