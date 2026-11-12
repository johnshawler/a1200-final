/* k2ge.c - draws one scanline of the K2GE picture (colour mode) from the chip's memory. See k2ge.h.
 *
 * Layers, back to front: background colour, sprites of priority 1, the back scroll plane, sprites of priority 2,
 * the front scroll plane, sprites of priority 3. Scroll plane 1 is the front one unless bit 7 of 0x8030 is set.
 * Colour 0 of every palette is transparent. Among sprites of one priority the lower number wins.
 * Not done: the mono (K1GE) mode of Neo Geo Pocket games without colour.
 */
#include "k2ge.h"

#define RD16(p) ((uint16_t)((p)[0] | (p)[1] << 8))

static void tile_row(const uint8_t *v, uint8_t *out, uint8_t *z, int x, unsigned tile, int row, int mirror,
                     unsigned pal, int depth, int lo, int hi)
{
    unsigned bits = RD16(v + 0x2000 + tile * 16 + row * 2);         /* leftmost pixel in bits 15-14 */
    int i;
    for (i = 0; i < 8; i++) {
        int px = x + i, c = mirror ? (bits >> (i * 2)) & 3 : (bits >> ((7 - i) * 2)) & 3;
        if (!c || px < lo || px > hi || depth <= z[px]) continue;
        z[px] = (uint8_t)depth;
        out[px] = (uint8_t)(pal + c);
    }
}

static void plane(const uint8_t *v, uint8_t *out, uint8_t *z, int line, int which, int depth, int lo, int hi)
{
    const uint8_t *map = v + (which ? 0x1800 : 0x1000);             /* 0x9000 and 0x9800: 32 x 32 words */
    unsigned pal = which ? 128 : 64;                                /* 0x8280 and 0x8300: 16 palettes of 4 */
    unsigned sx = v[which ? 0x34 : 0x32], sy = v[which ? 0x35 : 0x33];
    unsigned y = (line + sy) & 255, row = y & 7;
    int tx;
    for (tx = 0; tx < 32; tx++) {
        unsigned e = RD16(map + ((y >> 3) * 32 + tx) * 2);
        int x = (tx * 8 - sx) & 255;
        if (x > 248) x -= 256;
        if (x >= K2GE_W) continue;
        tile_row(v, out, z, x, e & 0x1ff, (e & 0x4000) ? 7 - row : row, e & 0x8000, pal + ((e >> 9) & 15) * 4, depth, lo, hi);
    }
}

void k2ge_line_idx(const uint8_t *v, int line, uint8_t *out)
{
    int winx = v[2], winy = v[3], winw = v[4], winh = v[5], x, i;
    int lo = winx, hi = (winx + winw < K2GE_W ? winx + winw : K2GE_W) - 1;
    unsigned edge = 0xf8 + (v[0x12] & 7), back = 0xf0 + (v[0x118] & 7);    /* window palette 0x83F0, background palette 0x83E0 */
    uint8_t z[K2GE_W];
    int lastx = 0, lasty = 0;
    for (x = 0; x < K2GE_W; x++) { out[x] = (uint8_t)edge; z[x] = 0; }
    if (line < winy || line >= winy + winh) return;                 /* above or below the window */
    for (x = lo; x <= hi; x++) out[x] = (uint8_t)back;
    if (v[0x30] & 0x80) { plane(v, out, z, line, 0, 3, lo, hi); plane(v, out, z, line, 1, 5, lo, hi); }
    else                { plane(v, out, z, line, 1, 3, lo, hi); plane(v, out, z, line, 0, 5, lo, hi); }
    for (i = 0; i < 64; i++) {                                      /* sprites: 0x8800, 4 bytes each */
        const uint8_t *s = v + 0x0800 + i * 4;
        unsigned e = RD16(s);
        int pr = (e >> 11) & 3, sx = s[2], sy = s[3], row;
        if (e & 0x0400) sx += lastx;                                /* chained to the sprite before */
        if (e & 0x0200) sy += lasty;
        lastx = sx; lasty = sy;
        if (!pr) continue;
        sx = (sx + v[0x20]) & 255; sy += v[0x21];                   /* 249..255 mean -7..-1 */
        if (sx > 248) sx -= 256;
        if (sy > 248 && sy < 256) sy -= 256; else sy &= 255;
        if (line < sy || line > sy + 7) continue;
        row = line - sy;
        tile_row(v, out, z, sx, e & 0x1ff, (e & 0x4000) ? 7 - row : row, e & 0x8000, (v[0x0c00 + i] & 15) * 4, pr * 2, lo, hi);
    }
}

void k2ge_line(const uint8_t *v, int line, uint16_t *out)
{
    uint8_t idx[K2GE_W];
    int x, neg = v[0x12] & 0x80;
    k2ge_line_idx(v, line, idx);
    for (x = 0; x < K2GE_W; x++) { unsigned c = RD16(v + 0x200 + idx[x] * 2); out[x] = (uint16_t)((neg ? ~c : c) & 0x0fff); }
}
