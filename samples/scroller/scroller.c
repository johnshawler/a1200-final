/* Scroller - PC version. 320x200, 256 colours, 60 Hz. Build: cc scroller.c ../common/pcfb_headless.c -I../common -lm
 * Data files (little-endian): palette.bin tiles.bin level.bin sprites.bin - see make_assets.py
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "pcfb.h"

#define W 320
#define H 200
#define VIEW_H 192
#define TILE 16
#define MAX_ENEMIES 10
#define MAX_BULLETS 6
#define MAX_BOOMS 6
#define STARS 48

#pragma pack(push, 1)
typedef struct { char magic[4]; uint16_t w, h, nspawn, pad; } level_header;
typedef struct { uint32_t x; uint16_t y, kind; } spawn_rec;
#pragma pack(pop)
typedef struct { int w, h; const uint8_t *pix; } sprite;
typedef struct { int on, kind, age; float x, y, y0; } enemy;
typedef struct { int on; float x, y; } bullet;
typedef struct { int on, age; float x, y; } boom;
typedef struct { float x; int y, layer; } star;

static uint8_t *fb;
static const uint8_t *tiles, *map;
static level_header *lvl;
static spawn_rec *spawns;
static sprite spr[16];
static enemy enemies[MAX_ENEMIES];
static bullet bullets[MAX_BULLETS];
static boom booms[MAX_BOOMS];
static star stars[STARS];
static float camx, plx = 40, ply = 90;
static int next_spawn, score, lives = 3, cooldown, invuln;

static const uint8_t font[10][5] = {
    {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
    {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7}
};

static int tile_at(float wx, float wy)
{
    int tx = (int)(wx / TILE), ty = (int)(wy / TILE);
    if (ty < 0 || ty >= lvl->h) return 0;
    return map[ty * lvl->w + tx % lvl->w];
}
static int solid(int t) { return t != 0 && t < 17; }

static void draw_sprite(const sprite *s, int x, int y)
{
    int i, j;
    for (j = 0; j < s->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= VIEW_H) continue;
        for (i = 0; i < s->w; i++) {
            int xx = x + i; uint8_t c = s->pix[j * s->w + i];
            if (c && xx >= 0 && xx < W) fb[yy * W + xx] = c;
        }
    }
}

static void draw_tiles(void)
{
    int cam = (int)camx, tx0 = cam / TILE, off = cam % TILE, tx, ty, i, j;
    for (ty = 0; ty < lvl->h; ty++)
        for (tx = 0; tx <= W / TILE; tx++) {
            int t = map[ty * lvl->w + (tx0 + tx) % lvl->w];
            const uint8_t *p = tiles + t * TILE * TILE;
            if (!t) continue;
            for (j = 0; j < TILE; j++)
                for (i = 0; i < TILE; i++) {
                    int x = tx * TILE + i - off; uint8_t c = p[j * TILE + i];
                    if (c && x >= 0 && x < W) fb[(ty * TILE + j) * W + x] = c;
                }
        }
}

static void number(int x, int y, int n, int digits)
{
    int d, r, k;
    for (d = digits - 1; d >= 0; d--, n /= 10)
        for (r = 0; r < 5; r++)
            for (k = 0; k < 3; k++)
                if (font[n % 10][r] & (4 >> k)) fb[(y + r) * W + x + d * 4 + k] = 255;
}

static void add_boom(float x, float y)
{
    int i;
    for (i = 0; i < MAX_BOOMS; i++) if (!booms[i].on) { booms[i].on = 1; booms[i].age = 0; booms[i].x = x; booms[i].y = y; break; }
    fb_sound(1);
}

int main(void)
{
    uint8_t *raw; const uint8_t *p; long size; int i, j, n;

    if (!fb_open(W, H, "Scroller")) return 1;
    fb = fb_pixels();
    raw = fb_load("palette.bin", &size); if (!raw) return 1;
    fb_palette((fb_color *)raw, 0, 256);
    raw = fb_load("tiles.bin", &size); if (!raw) return 1;
    tiles = raw + 4;
    raw = fb_load("level.bin", &size); if (!raw) return 1;
    lvl = (level_header *)raw; map = raw + sizeof(level_header);
    spawns = (spawn_rec *)(map + lvl->w * lvl->h);
    raw = fb_load("sprites.bin", &size); if (!raw) return 1;
    n = *(uint16_t *)raw; p = raw + 2;
    for (i = 0; i < n; i++) { spr[i].w = *(uint16_t *)p; spr[i].h = *(uint16_t *)(p + 2); spr[i].pix = p + 4; p += 4 + spr[i].w * spr[i].h; }
    for (i = 0; i < STARS; i++) { stars[i].x = (float)((i * 97) % W); stars[i].y = (i * 53) % VIEW_H; stars[i].layer = i % 3; }

    while (!fb_key(FBK_QUIT)) {
        /* ---- update ---- */
        camx += 1.25f;
        if (camx >= (float)(lvl->w * TILE)) { camx -= (float)(lvl->w * TILE); next_spawn = 0; for (i = 0; i < MAX_ENEMIES; i++) enemies[i].on = 0; }
        if (fb_key(FBK_LEFT))  plx -= 2.5f;
        if (fb_key(FBK_RIGHT)) plx += 2.5f;
        if (fb_key(FBK_UP))    ply -= 2.5f;
        if (fb_key(FBK_DOWN))  ply += 2.5f;
        if (plx < 0) plx = 0;
        if (plx > W - 32) plx = W - 32;
        if (ply < 0) ply = 0;
        if (ply > VIEW_H - 16) ply = VIEW_H - 16;
        if (cooldown > 0) cooldown--;
        if (invuln > 0) invuln--;
        if (fb_key(FBK_FIRE) && cooldown == 0)
            for (i = 0; i < MAX_BULLETS; i++) if (!bullets[i].on) { bullets[i].on = 1; bullets[i].x = plx + 28; bullets[i].y = ply + 6; cooldown = 8; fb_sound(0); break; }
        for (i = 0; i < MAX_BULLETS; i++) if (bullets[i].on) {
            bullets[i].x += 6.0f;
            if (bullets[i].x > W || solid(tile_at(camx + bullets[i].x + 4, bullets[i].y + 2))) bullets[i].on = 0;
        }
        while (next_spawn < lvl->nspawn && (float)spawns[next_spawn].x < camx + W + 20) {
            for (i = 0; i < MAX_ENEMIES; i++) if (!enemies[i].on) {
                enemies[i].on = 1; enemies[i].kind = spawns[next_spawn].kind; enemies[i].age = 0;
                enemies[i].x = (float)spawns[next_spawn].x; enemies[i].y = enemies[i].y0 = (float)spawns[next_spawn].y; break;
            }
            next_spawn++;
        }
        for (i = 0; i < MAX_ENEMIES; i++) if (enemies[i].on) {
            enemy *e = &enemies[i]; float sx;
            e->age++;
            if (e->kind == 0) { e->x -= 1.0f; e->y = e->y0 + 24.0f * sinf(e->age * 0.08f); }
            else { e->x -= 1.6f; e->y += (ply > e->y) ? 0.8f : -0.8f; }
            sx = e->x - camx;
            if (sx < -20) { e->on = 0; continue; }
            for (j = 0; j < MAX_BULLETS; j++)
                if (bullets[j].on && bullets[j].x + 8 > sx && bullets[j].x < sx + 16 && bullets[j].y + 4 > e->y && bullets[j].y < e->y + 16) {
                    bullets[j].on = 0; e->on = 0; score += 100; add_boom(sx, e->y); break;
                }
            if (e->on && !invuln && plx + 30 > sx && plx + 2 < sx + 16 && ply + 14 > e->y && ply + 2 < e->y + 16) {
                e->on = 0; add_boom(sx, e->y); lives--; invuln = 120; fb_sound(2);
            }
        }
        if (!invuln && (solid(tile_at(camx + plx + 28, ply + 8)) || solid(tile_at(camx + plx + 8, ply + 2)) || solid(tile_at(camx + plx + 8, ply + 13)))) {
            add_boom(plx + 8, ply); lives--; invuln = 120; ply = 90; fb_sound(2);
        }
        if (lives <= 0) { lives = 3; score = 0; }
        for (i = 0; i < MAX_BOOMS; i++) if (booms[i].on) { booms[i].x -= 1.25f; if (++booms[i].age >= 24) booms[i].on = 0; }
        for (i = 0; i < STARS; i++) { stars[i].x -= 0.25f * (stars[i].layer + 1); if (stars[i].x < 0) stars[i].x += W; }

        /* ---- draw the whole frame ---- */
        memset(fb, 0, W * H);
        for (i = 0; i < STARS; i++) fb[stars[i].y * W + (int)stars[i].x] = (uint8_t)(112 + stars[i].layer);
        draw_tiles();
        for (i = 0; i < MAX_ENEMIES; i++) if (enemies[i].on) draw_sprite(&spr[1 + (enemies[i].age / 8) % 2], (int)(enemies[i].x - camx), (int)enemies[i].y);
        for (i = 0; i < MAX_BULLETS; i++) if (bullets[i].on) draw_sprite(&spr[3], (int)bullets[i].x, (int)bullets[i].y);
        if (!(invuln & 4)) draw_sprite(&spr[0], (int)plx, (int)ply);
        for (i = 0; i < MAX_BOOMS; i++) if (booms[i].on) draw_sprite(&spr[4 + booms[i].age / 6], (int)booms[i].x, (int)booms[i].y);
        memset(fb + VIEW_H * W, 16, W * (H - VIEW_H));
        number(4, VIEW_H + 2, score, 6);
        for (i = 0; i < lives; i++) for (j = 0; j < 4; j++) memset(fb + (VIEW_H + 2 + j) * W + W - 12 - i * 10, 70, 6);
        fb_flip();
    }
    fb_close();
    return 0;
}
