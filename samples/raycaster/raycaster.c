/* Raycaster - PC version. 320x200, 256 colours, 60 Hz. Build: cc raycaster.c ../common/pcfb_headless.c -I../common -lm */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pcfb.h"

#define W 320
#define H 200
#define MAPW 24
#define MAPH 24
#define TEX 64
#define SHADES 3
#define MARGIN 0.3

static const char *map[MAPH] = {
    "111111111111111111111111",
    "1......................1",
    "1..222....33333.....4..1",
    "1..2.2....3...3.....4..1",
    "1..222....3...3.....4..1",
    "1.........33.33.....4..1",
    "1...................4..1",
    "1....4.4.4.4........4..1",
    "1......................1",
    "1..11111......222222...1",
    "1..1...1......2....2...1",
    "1..1...1...........2...1",
    "1..1...1......2....2...1",
    "1..11.11......222222...1",
    "1......................1",
    "1......................1",
    "1.3.3.3.3.....4444.....1",
    "1.............4........1",
    "1.3.3.3.3.....4..4.....1",
    "1.............4444.....1",
    "1......................1",
    "1..2..2..2..2..2..2....1",
    "1......................1",
    "111111111111111111111111"
};

static uint8_t tex[4][TEX * TEX];       /* texel = colour 0..15 inside the texture's 16-colour ramp */
static uint8_t *fb;

static void make_textures(void)
{
    int t, x, y;
    for (t = 0; t < 4; t++)
        for (y = 0; y < TEX; y++)
            for (x = 0; x < TEX; x++) {
                int v;
                switch (t) {
                case 0:  v = ((y % 16) == 0 || ((x + (y / 16 % 2) * 16) % 32) == 0) ? 3 : 9 + ((x * 7 + y * 13) % 5); break;   /* bricks */
                case 1:  v = ((x ^ y) >> 2) & 15; break;                                                                   /* xor */
                case 2:  v = 4 + (int)(5.5f + 5.5f * sinf(x * 0.2f) * cosf(y * 0.2f)); break;                              /* blobs */
                default: v = (x % 16 < 2 || y % 16 < 2) ? 2 : 12 + ((x / 16 + y / 16) & 1) * 3; break;                       /* tiles */
                }
                tex[t][y * TEX + x] = (uint8_t)(v & 15);
            }
}

int main(void)
{
    fb_color pal[256];
    double px = 12.5, py = 14.5, dirx = 1.0, diry = 0.0, planex = 0.0, planey = 0.66;
    double last;
    int t, s, i, x, y;

    if (!fb_open(W, H, "Raycaster")) return 1;
    fb = fb_pixels();
    memset(pal, 0, sizeof pal);
    /* colour = 16 + texture*48 + shade*16 + texel ; shade 0 = near (bright) .. 2 = far (dark) */
    for (t = 0; t < 4; t++)
        for (s = 0; s < SHADES; s++)
            for (i = 0; i < 16; i++) {
                static const float base[4][3] = { {1.0f, 0.45f, 0.3f}, {0.4f, 0.9f, 1.0f}, {0.5f, 1.0f, 0.4f}, {1.0f, 0.9f, 0.5f} };
                float k = (40 + i * 14) * (1.0f - s * 0.3f);
                fb_color *c = &pal[16 + t * 48 + s * 16 + i];
                c->r = (uint8_t)(k * base[t][0]); c->g = (uint8_t)(k * base[t][1]); c->b = (uint8_t)(k * base[t][2]);
            }
    pal[1].r = 40; pal[1].g = 40; pal[1].b = 70;      /* ceiling */
    pal[2].r = 70; pal[2].g = 60; pal[2].b = 50;      /* floor */
    fb_palette(pal, 0, 256);
    make_textures();
    last = fb_time();

    while (!fb_key(FBK_QUIT)) {
        double now = fb_time(), dt = now - last, move = dt * 3.0, rot = dt * 2.0;
        last = now;
        if (fb_key(FBK_UP) || fb_key(FBK_DOWN)) {
            double sgn = fb_key(FBK_UP) ? 1.0 : -1.0;
            double vx = dirx * move * sgn, vy = diry * move * sgn;
            double nx = px + vx, ny = py + vy;
            if (map[(int)py][(int)(nx + (vx > 0 ? MARGIN : -MARGIN))] == '.') px = nx;      /* keep away from walls */
            if (map[(int)(ny + (vy > 0 ? MARGIN : -MARGIN))][(int)px] == '.') py = ny;
        }
        if (fb_key(FBK_RIGHT) || fb_key(FBK_LEFT)) {
            double a = fb_key(FBK_RIGHT) ? rot : -rot, od = dirx, op = planex;
            dirx = dirx * cos(a) - diry * sin(a);     diry = od * sin(a) + diry * cos(a);
            planex = planex * cos(a) - planey * sin(a); planey = op * sin(a) + planey * cos(a);
        }

        for (x = 0; x < W; x++) {
            double cam = 2.0 * x / W - 1.0, rdx = dirx + planex * cam, rdy = diry + planey * cam;
            int mx = (int)px, my = (int)py, stepx, stepy, side = 0, hit = 0, h, y0, y1, tx, shade, tn;
            double ddx = fabs(1.0 / rdx), ddy = fabs(1.0 / rdy), sdx, sdy, dist, wallx, tstep, tpos;
            if (rdx < 0) { stepx = -1; sdx = (px - mx) * ddx; } else { stepx = 1; sdx = (mx + 1.0 - px) * ddx; }
            if (rdy < 0) { stepy = -1; sdy = (py - my) * ddy; } else { stepy = 1; sdy = (my + 1.0 - py) * ddy; }
            while (!hit) {
                if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; } else { sdy += ddy; my += stepy; side = 1; }
                if (map[my][mx] != '.') hit = 1;
            }
            dist = side ? sdy - ddy : sdx - ddx;
            if (dist < 0.05) dist = 0.05;
            h = (int)(H / dist);
            y0 = H / 2 - h / 2; y1 = H / 2 + h / 2;
            wallx = side ? px + dist * rdx : py + dist * rdy;
            wallx -= floor(wallx);
            tx = (int)(wallx * TEX);
            tn = map[my][mx] - '1';
            shade = (int)(dist / 3.0) + side; if (shade > SHADES - 1) shade = SHADES - 1;
            tstep = (double)TEX / h; tpos = (y0 < 0 ? -y0 : 0) * tstep;
            for (y = 0; y < H; y++) {
                uint8_t c;
                if (y < y0) c = 1;
                else if (y >= y1) c = 2;
                else { c = (uint8_t)(16 + tn * 48 + shade * 16 + tex[tn][((int)tpos & (TEX - 1)) * TEX + tx]); tpos += tstep; }
                fb[y * W + x] = c;
            }
        }
        fb_flip();
    }
    fb_close();
    return 0;
}
