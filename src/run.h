/* run.h - enter the translated NES code: the C translation (run_c.c, host test) or the 68k one (run68k.c,
 * Amiga). Each returns the 6502 address where execution continues (an RTS nobody called from). */
#ifndef RUN_H
#define RUN_H
#include <stdint.h>
uint16_t run_reset(void);
uint16_t run_nmi(void);
uint16_t run_dispatch(uint16_t pc);
#endif
