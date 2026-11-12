/* host_render.c - host test only: draw the NES picture from the PPU state (nes.h), as the NES would show it
 * with the split taken into account, and write it as a PPM. For looking at what the logic did. */
#include <stdio.h>
#include <string.h>
#include "nes.h"

static const uint32_t pal64[64] = {
0x666666,0x002a88,0x1412a7,0x3b00a4,0x5c007e,0x6e0040,0x6c0600,0x561d00,0x333500,0x0b4800,0x005200,0x004f08,0x00404d,0,0,0,
0xadadad,0x155fd9,0x4240ff,0x7527fe,0xa01acc,0xb71e7b,0xb53120,0x994e00,0x6b6d00,0x388700,0x0c9300,0x008f32,0x007c8d,0,0,0,
0xfffeff,0x64b0ff,0x9290ff,0xc676ff,0xf36aff,0xfe6ecc,0xfe8170,0xea9e22,0xbcbe00,0x88d800,0x5ce430,0x45e082,0x48cdde,0x4f4f4f,0,0,
0xfffeff,0xc0dfff,0xd3d2ff,0xe8c8ff,0xfbc2ff,0xfec4ea,0xfeccc5,0xf7d8a5,0xe4e594,0xcfef96,0xbdf4ab,0xb3f3cc,0xb5ebf2,0xb8b8b8,0,0};

static uint8_t chr_byte(uint8_t chr0, uint8_t chr1, uint16_t a)
{
    uint8_t bank = (mmc_ctrl & 0x10) ? ((a < 0x1000) ? chr0 : chr1) : (uint8_t)((chr0 & 0x1e) | (a >> 12));
    return nes_chr[(uint32_t)(bank & 31) * 0x1000 + (a & 0xfff)];
}
static uint16_t nti(int nt, int idx) { int m = mmc_ctrl & 3; nt &= 3;
    if (m == 0) return idx; if (m == 1) return 0x400 | idx; if (m == 2) return ((nt & 1) << 10) | idx; return ((nt >> 1) << 10) | idx; }

void host_render(const char *path)
{
    static uint8_t px[240][256]; int x, y, i;
    FILE *f;
    for (y = 0; y < 240; y++) {
        int lower = split_on && y > split_line;
        uint8_t ctrl = lower || !split_on ? ppu_ctrl : split_ctrl;
        uint8_t c0 = lower || !split_on ? mmc_chr0 : split_chr0, c1 = lower || !split_on ? mmc_chr1 : split_chr1;
        uint16_t t = lower || !split_on ? ppu_t : split_t; uint8_t fx = lower || !split_on ? ppu_fx : split_fx;
        int sx = ((t & 0x1f) << 3 | fx) + ((t >> 10) & 1) * 256;
        int sy = ((t >> 5) & 0x1f) * 8 + ((t >> 12) & 7) + ((t >> 11) & 1) * 240;
        if (lower) { int ys = ((split_t >> 5) & 0x1f) * 8 + ((split_t >> 12) & 7) + ((split_t >> 11) & 1) * 240; sy = ys; }
        for (x = 0; x < 256; x++) {
            int wx = (sx + x) & 511, wy = (sy + y) % 480, nt = (wx >> 8) | ((wy / 240) << 1), tx = (wx & 255) >> 3, ty = (wy % 240) >> 3;
            uint8_t tile = nes_vram[nti(nt, ty * 32 + tx)], at = nes_vram[nti(nt, 0x3c0 + (ty >> 2) * 8 + (tx >> 2))];
            uint16_t pa = ((ctrl & 0x10) ? 0x1000 : 0) + tile * 16 + ((wy % 240) & 7);
            int b = 7 - (wx & 7), c = ((chr_byte(c0, c1, pa) >> b) & 1) | (((chr_byte(c0, c1, pa + 8) >> b) & 1) << 1);
            at = (at >> (((ty & 2) << 1) | (tx & 2))) & 3;
            px[y][x] = c ? (at << 2) | c : 0;
            if (!(ppu_mask & 0x08) || (x < 8 && !(ppu_mask & 2))) px[y][x] = 0;
        }
    }
    if (ppu_mask & 0x10)
        for (i = 63; i >= 0; i--) {
            int sy = nes_oam[i * 4] + 1, tile = nes_oam[i * 4 + 1], at = nes_oam[i * 4 + 2], sx = nes_oam[i * 4 + 3], r, b;
            for (r = 0; r < 8; r++) {
                int yy = sy + r, row = (at & 0x80) ? 7 - r : r; uint16_t pa = ((ppu_ctrl & 0x08) ? 0x1000 : 0) + tile * 16 + row;
                uint8_t p0 = chr_byte(mmc_chr0, mmc_chr1, pa), p1 = chr_byte(mmc_chr0, mmc_chr1, pa + 8);
                if (yy >= 240) continue;
                for (b = 0; b < 8; b++) {
                    int bit = (at & 0x40) ? b : 7 - b, c = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1);
                    if (!c || sx + b > 255) continue;
                    if ((at & 0x20) && (px[yy][sx + b] & 3)) continue;
                    px[yy][sx + b] = 0x10 | ((at & 3) << 2) | c;
                }
            }
        }
    f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n256 240\n255\n");
    for (y = 0; y < 240; y++) for (x = 0; x < 256; x++) {
        uint8_t v = px[y][x]; uint32_t c = pal64[(v & 3) ? nes_pal[v & 0x1f] : nes_pal[0]];
        fputc(c >> 16, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
    }
    fclose(f);
}
