/* host shim (tools/hostinc): the parts of the custom chip registers src/sound.c uses */
#ifndef HW_CUSTOM_H
#define HW_CUSTOM_H
#include <exec/types.h>
struct AudChannel { UWORD *ac_ptr; UWORD ac_len, ac_per, ac_vol, ac_dat; };
struct Custom { struct AudChannel aud[4]; UWORD dmacon; };
#endif
