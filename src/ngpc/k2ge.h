/* k2ge.h - the Neo Geo Pocket Color video chip (K2GE) as a function of its memory: one scanline at a time.
 * Plain C, no platform header. Used by the reference emulator (tools/ngpref), by the host test's pictures and
 * by the Amiga port's first, correct-but-slow display. */
#ifndef K2GE_H
#define K2GE_H
#include <stdint.h>
#define K2GE_W 160
#define K2GE_H_LINES 152
/* v = the 16 KB at 0x8000-0xBFFF. out = 160 colours, 12 bits each (0x0BGR as the palette RAM holds them). */
void k2ge_line(const uint8_t *v, int line, uint16_t *out);
#endif
