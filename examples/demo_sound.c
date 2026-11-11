/* demo_sound.c - music and sound effects with Frank Wille's ptplayer under the kit.
 * Plays the module for ten seconds with one effect a second, then quits. Escape quits earlier.
 * Build: CFLAGS="-Ikit" tools/build.sh dist/demo_sound kit/sys.c examples/demo_sound.c examples/sound_data.s kit/ptplayer/ptplayer.asm
 * Ran on an emulated stock A1200: the player took 0.5% of the CPU.
 */
#include <proto/exec.h>
#include "sys.h"

void mt_install(void *custom_base __asm("a6"), void *vbr __asm("a0"), UBYTE pal __asm("d0"));
void mt_remove(void *custom_base __asm("a6"));
void mt_init(void *custom_base __asm("a6"), void *module __asm("a0"), void *samples __asm("a1"), UBYTE pos __asm("d0"));
void mt_end(void *custom_base __asm("a6"));
typedef struct { void *sfx_ptr; UWORD sfx_len, sfx_per, sfx_vol; BYTE sfx_cha, sfx_pri; } SfxStructure;
void *mt_playfx(void *custom_base __asm("a6"), SfxStructure *sfx __asm("a0"));
extern UBYTE mt_Enable;
extern UBYTE mod_data[], fx_data[];     /* examples/sound_data.s */

int main(void)
{
    SfxStructure fx;
    int frame;
    if (!sys_take()) return 20;
    sys_vbl_hook = sys_key_poll;
    mt_install((void *)0xdff000, sys_vbr, sys_is_pal);          /* after sys_take() */
    mt_init((void *)0xdff000, mod_data, 0, 0);
    mt_Enable = 1;                                              /* music plays from an interrupt */
    fx.sfx_ptr = fx_data; fx.sfx_len = 200; fx.sfx_per = 322; fx.sfx_vol = 64; fx.sfx_cha = -1; fx.sfx_pri = 10;
    for (frame = 0; frame < 500 && !sys_keys[KEY_ESC]; frame++) {
        if (frame % 50 == 25) mt_playfx((void *)0xdff000, &fx); /* any time during the game */
        sys_wait_frame();
    }
    mt_Enable = 0;
    mt_end((void *)0xdff000);
    mt_remove((void *)0xdff000);                                /* before sys_give() */
    sys_give();
    return 0;
}
