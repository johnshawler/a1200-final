/* demo_chunky.c - a byte-per-pixel renderer at 160x128, shown at 320x256 (each pixel doubled both ways).
 *   demo_chunky bench N   run N displayed frames, write bench.txt
 * The chunky width must be a multiple of 32 (160, 128, 96): the C2P routine rounds it down.
 * Chunky-to-planar: Mikael Kalms' public-domain c2p2x1_8_c5_gen (kit/c2p).
 */
#include <proto/exec.h>
#include <exec/memory.h>
#include <stdlib.h>
#include <string.h>
#include "sys.h"
#include "disp.h"
#include "perf.h"

void c2p2x1_8_c5_gen_init(int cx __asm("d0"), int cy __asm("d1"), int offx __asm("d2"), int offy __asm("d3"),
                          int rowlen __asm("d4"), int bplsize __asm("d5"), int chunkylen __asm("d6"));
void c2p2x1_8_c5_gen(void *chunky __asm("a0"), void *planes __asm("a1"));

#define CW 160
#define CH 128

static UBYTE *chunky;                   /* CW x CH bytes, one byte per pixel */
static UBYTE tex[256];
static ULONG pal[256];
static ULONG gametime;

static void tick(void) { gametime++; }                  /* game logic: 50 times a second, whatever the frame rate */

static void render(void)                                /* stand-in for a software renderer: one table look-up per pixel */
{
    UBYTE *d = chunky; int x, y; UBYTE t = (UBYTE)gametime;
    for (y = 0; y < CH; y++) {
        UBYTE row = tex[(y + t) & 255];
        for (x = 0; x < CW; x++) *d++ = tex[(x + row) & 255];
    }
}

int main(int argc, char **argv)
{
    static const DispCfg cfg = { .planes = 8, .scroll = 0, .fetch = 3, .view_h = 256, .buf_h = CH,
                                 .level_w = 0, .buffers = 2, .double_y = 1, .sprite_w = 16, .sprite_bank = 1 };
    ULONG bench = (argc > 2 && !strcmp(argv[1], "bench")) ? strtoul(argv[2], 0, 10) : 0;
    ULONG last, now, owed, t0, frames = 0;
    int i;

    chunky = sys_alloc(CW * CH, MEMF_ANY | MEMF_CLEAR);  /* fast RAM if the machine has any */
    if (!chunky || !disp_open(&cfg)) { disp_close(); sys_free_all(); return 20; }
    for (i = 0; i < 256; i++) { tex[i] = (UBYTE)(i < 128 ? i * 2 : 510 - i * 2); pal[i] = ((ULONG)i << 16) | ((ULONG)(255 - i) << 8) | (ULONG)((i * 5) & 255); }
    if (!sys_take()) { disp_close(); sys_free_all(); return 20; }
    sys_vbl_hook = sys_key_poll;
    c2p2x1_8_c5_gen_init(CW, CH, 0, 0, disp_row, disp_plane, CW);
    disp_palette(pal, 0, 256);
    disp_on();
    perf_reset();
    sys_wait_frame();
    last = sys_frame;
    for (;;) {
        t0 = sys_now();
        now = sys_frame; owed = now - last; last = now;
        if (frames == 0) owed = 1;
        perf_frame_start(owed);
        if (owed > 10) owed = 10;
        while (owed--) tick();
        render();
        c2p2x1_8_c5_gen(chunky, disp_back);
        frames++;
        perf_frame_end(sys_now() - t0);
        if (sys_keys[KEY_ESC] || (bench && frames >= bench)) break;
        disp_flip();
    }
    sys_give();
    disp_close();
    sys_free_all();
    if (bench) perf_save("bench.txt");
    return 0;
}
