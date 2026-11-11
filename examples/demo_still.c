/* demo_still.c - a still screen: a whole background picture, pieces stamped onto it, one area erased again,
 * and one moving object. 5 planes (32 colours), fixed screen. Ran on an emulated stock A1200. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include "sys.h"
#include "disp.h"
int main(void)
{
    static const DispCfg cfg = { .planes = 5, .scroll = 0, .fetch = 3, .view_h = 256, .buf_h = 256, .level_w = 0, .buffers = 3, .double_y = 0, .sprite_w = 16, .sprite_bank = 4 };
    static ULONG pal[32];
    UBYTE *pic = sys_alloc(5 * 40 * 256, MEMF_CHIP | MEMF_CLEAR);
    UWORD *img = sys_alloc(5 * 8 * 2 * 2 + 16, MEMF_CHIP | MEMF_CLEAR), *msk = sys_alloc(8 * 2 * 2 + 16, MEMF_CHIP | MEMF_CLEAR);
    int x, y, p, i;
    if (!pic || !img || !msk || !disp_open(&cfg)) { disp_close(); sys_free_all(); return 20; }
    for (y = 0; y < 256; y++) for (x = 0; x < 320; x++) { int c = 1 + ((x >> 4) + (y >> 4)) % 15; for (p = 0; p < 5; p++) if (c & (1 << p)) pic[p * 40 * 256 + y * 40 + (x >> 3)] |= 0x80 >> (x & 7); }
    for (y = 0; y < 8; y++) { msk[y * 2] = 0xffff; msk[y * 2 + 1] = 0xe000; for (p = 0; p < 5; p++) if (31 & (1 << p)) { img[(p * 8 + y) * 2] = 0xffff; img[(p * 8 + y) * 2 + 1] = 0xe000; } }   /* a 19x8 block, colour 31 */
    for (i = 0; i < 32; i++) pal[i] = ((ULONG)(i * 8) << 16) | ((ULONG)(255 - i * 8) << 8) | 0x80; pal[31] = 0xffffff;
    if (!sys_take()) { disp_close(); sys_free_all(); return 20; }
    sys_vbl_hook = sys_key_poll;
    disp_palette(pal, 0, 32); disp_on();
    disp_paste(pic, 0, 0, 320, 256);                                    /* whole background */
    for (i = 0; i < 10; i++) disp_stamp(20 + i * 20, 40, img, msk, 2, 8); /* a row of bricks at x = 20, 40, ... */
    disp_paste(pic, 48, 32, 48, 32);                                      /* erase the area covering bricks 2 and 3 */
    for (i = 0; i < 3000 && !sys_keys[KEY_ESC]; i++) { disp_begin(); disp_bob(100 + (i & 63), 120, img, msk, 2, 8); disp_flip(); }
    sys_give(); disp_close(); sys_free_all();
    return 0;
}
