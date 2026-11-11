/* pcfb_headless.c - host backend without a window: scripted input, virtual 60 Hz clock, PPM dumps.
 * Env: FB_FRAMES=n      quit after n frames (default 600)
 *      FB_DUMP=a,b,c    write frame_a.ppm ... for these frame numbers
 *      FB_INPUT=file    lines "first last KEY" (KEY = LEFT RIGHT UP DOWN FIRE), frame ranges inclusive
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pcfb.h"

static int W, H, frame, max_frames = 600;
static uint8_t *pix;
static fb_color pal[256];
static struct { int a, b, k; } script[256];
static int nscript;
static int dump[64], ndump;
static int sounds;

int fb_open(int w, int h, const char *title)
{
    const char *e;
    (void)title;
    W = w; H = h; pix = calloc(w, h);
    if ((e = getenv("FB_FRAMES"))) max_frames = atoi(e);
    if ((e = getenv("FB_DUMP"))) { char *s = strdup(e), *t = strtok(s, ","); while (t && ndump < 64) { dump[ndump++] = atoi(t); t = strtok(NULL, ","); } }
    if ((e = getenv("FB_INPUT"))) {
        FILE *f = fopen(e, "r"); char key[16]; int a, b;
        static const char *names[] = { "LEFT", "RIGHT", "UP", "DOWN", "FIRE" };
        while (f && nscript < 256 && fscanf(f, "%d %d %15s", &a, &b, key) == 3) {
            int k; for (k = 0; k < 5; k++) if (!strcmp(key, names[k])) { script[nscript].a = a; script[nscript].b = b; script[nscript].k = k; nscript++; }
        }
        if (f) fclose(f);
    }
    return pix != NULL;
}
uint8_t *fb_pixels(void) { return pix; }
void fb_palette(const fb_color *p, int first, int count) { memcpy(pal + first, p, count * sizeof(fb_color)); }
void fb_flip(void)
{
    int i, j;
    for (i = 0; i < ndump; i++) if (dump[i] == frame) {
        char name[64]; FILE *f;
        sprintf(name, "frame_%04d.ppm", frame);
        f = fopen(name, "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (j = 0; j < W * H; j++) fwrite(&pal[pix[j]], 1, 3, f);
        fclose(f);
    }
    frame++;
}
int fb_key(int key)
{
    int i;
    if (key == FBK_QUIT) return frame >= max_frames;
    for (i = 0; i < nscript; i++) if (script[i].k == key && frame >= script[i].a && frame <= script[i].b) return 1;
    return 0;
}
double fb_time(void) { return frame / 60.0; }
void *fb_load(const char *path, long *size)
{
    FILE *f = fopen(path, "rb"); long n; void *p;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc(n); if (fread(p, 1, n, f) != (size_t)n) { free(p); p = NULL; }
    fclose(f); if (size) *size = n;
    return p;
}
void fb_sound(int id) { (void)id; sounds++; }
void fb_close(void) { fprintf(stderr, "frames=%d sounds=%d\n", frame, sounds); free(pix); }
