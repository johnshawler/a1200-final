/* Breakout - PC version. 320x240, 256 colours, 60 Hz. Build: cc breakout.c ../common/pcfb_headless.c -I../common -lm */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pcfb.h"

#define W 320
#define H 240
#define COLS 14
#define ROWS 8
#define BW 20
#define BH 8
#define BX0 20
#define BY0 40
#define PADW 36
#define PADY 220

static uint8_t *fb;
static uint8_t backdrop[W * H];
static int bricks[ROWS][COLS];
static float px = 160.0f, bx, by, bvx, bvy, speed = 3.2f;
static int score, lives = 3, stuck = 1, left;

static const uint8_t font[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7}
};

static void rect(int x, int y, int w, int h, int c)
{
    int i, j;
    for (j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (i = x; i < x + w; i++) if (i >= 0 && i < W) fb[j * W + i] = (uint8_t)c;
    }
}

static void number(int x, int y, int n, int digits, int c)
{
    int d, r, k;
    for (d = digits - 1; d >= 0; d--, n /= 10)
        for (r = 0; r < 5; r++)
            for (k = 0; k < 3; k++)
                if (font[n % 10][r] & (4 >> k)) rect(x + d * 8 + k * 2, y + r * 2, 2, 2, c);
}

static void make_backdrop(void)
{
    int x, y;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            float d = sqrtf((float)((x - 160) * (x - 160) + (y - 120) * (y - 120)));
            backdrop[y * W + x] = (uint8_t)(16 + ((int)(d * 0.25f) + ((x ^ y) & 8 ? 1 : 0)) % 32);
        }
}

static void new_ball(void) { stuck = 1; speed = 3.2f; }

int main(void)
{
    fb_color pal[256];
    int i, r, c;

    if (!fb_open(W, H, "Breakout")) return 1;
    fb = fb_pixels();
    memset(pal, 0, sizeof pal);
    pal[1].r = pal[1].g = pal[1].b = 255;
    for (i = 0; i < 8; i++) {                       /* brick colours, one per row: 2..9 */
        pal[2 + i].r = (uint8_t)(255 - i * 20); pal[2 + i].g = (uint8_t)(40 + i * 25); pal[2 + i].b = (uint8_t)(60 + (i & 1) * 120);
    }
    pal[10].r = 200; pal[10].g = 200; pal[10].b = 220;   /* paddle */
    pal[11].r = 255; pal[11].g = 240; pal[11].b = 80;    /* ball */
    for (i = 0; i < 32; i++) {                      /* backdrop gradient: 16..47 */
        int v = i < 16 ? i : 31 - i;
        pal[16 + i].r = (uint8_t)(v * 3); pal[16 + i].g = (uint8_t)(v * 4); pal[16 + i].b = (uint8_t)(40 + v * 8);
    }
    fb_palette(pal, 0, 256);
    make_backdrop();
    for (r = 0; r < ROWS; r++) for (c = 0; c < COLS; c++) bricks[r][c] = 1;
    left = ROWS * COLS;

    while (!fb_key(FBK_QUIT)) {
        if (fb_key(FBK_LEFT))  px -= 4.5f;
        if (fb_key(FBK_RIGHT)) px += 4.5f;
        if (px < PADW / 2) px = PADW / 2;
        if (px > W - PADW / 2) px = W - PADW / 2;

        if (stuck) {
            bx = px; by = PADY - 4;
            if (fb_key(FBK_FIRE)) { stuck = 0; bvx = speed * 0.5f; bvy = -speed * 0.866f; }
        } else {
            bx += bvx; by += bvy;
            if (bx < 2)     { bx = 2;     bvx = -bvx; fb_sound(0); }
            if (bx > W - 3) { bx = W - 3; bvx = -bvx; fb_sound(0); }
            if (by < 14)    { by = 14;    bvy = -bvy; fb_sound(0); }
            if (bvy > 0 && by >= PADY - 3 && by <= PADY + 4 && fabsf(bx - px) < PADW / 2 + 2) {
                float a = (bx - px) / (PADW / 2) * 1.05f;        /* -1.05 .. 1.05 rad from vertical */
                speed += 0.05f;
                bvx = speed * sinf(a); bvy = -speed * cosf(a);
                fb_sound(1);
            }
            if (by > H) { lives--; new_ball(); fb_sound(3); if (lives == 0) { lives = 3; score = 0; left = ROWS * COLS; for (r = 0; r < ROWS; r++) for (c = 0; c < COLS; c++) bricks[r][c] = 1; } }
            c = (int)floorf((bx - BX0) / BW); r = (int)floorf((by - BY0) / BH);
            if (r >= 0 && r < ROWS && c >= 0 && c < COLS && bricks[r][c]) {
                float cx = BX0 + c * BW + BW / 2.0f, cy = BY0 + r * BH + BH / 2.0f;
                bricks[r][c] = 0; left--; score += 10 * (ROWS - r);
                if (fabsf(bx - cx) / BW > fabsf(by - cy) / BH) bvx = -bvx; else bvy = -bvy;
                fb_sound(2);
                if (left == 0) { left = ROWS * COLS; for (r = 0; r < ROWS; r++) for (c = 0; c < COLS; c++) bricks[r][c] = 1; new_ball(); }
            }
        }

        /* draw the whole frame */
        memcpy(fb, backdrop, W * H);
        for (r = 0; r < ROWS; r++)
            for (c = 0; c < COLS; c++)
                if (bricks[r][c]) {
                    rect(BX0 + c * BW, BY0 + r * BH, BW - 1, BH - 1, 2 + r);
                    rect(BX0 + c * BW, BY0 + r * BH, BW - 1, 1, 1);
                }
        rect((int)px - PADW / 2, PADY, PADW, 6, 10);
        rect((int)px - PADW / 2 + 2, PADY + 1, PADW - 4, 1, 1);
        rect((int)bx - 2, (int)by - 2, 5, 5, 11);
        number(8, 4, score, 6, 1);
        for (i = 0; i < lives; i++) rect(W - 16 - i * 12, 6, 8, 6, 10);
        fb_flip();
    }
    fb_close();
    return 0;
}
