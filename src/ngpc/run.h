/* run.h - enter the game's code: the C translation (run_c.c) or, for the proof, the interpreter (run_interp.c).
 * run_call executes from pc until that code returns (RET or RETI at its own level) and gives back the address
 * where execution goes on. */
#ifndef NGPC_RUN_H
#define NGPC_RUN_H
#include <stdint.h>
uint32_t run_call(uint32_t pc);
#endif
