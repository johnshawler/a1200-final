/* logic.h - the seam between the game (plain C, also built on the host) and the Amiga side. */
#ifndef LOGIC_H
#define LOGIC_H
#include <stdint.h>

/* input bits for game_tick(): the kit's joystick bits, plus two buttons the joystick does not have */
#define IN_RIGHT  1
#define IN_LEFT   2
#define IN_DOWN   4
#define IN_UP     8
#define IN_A      16            /* fire */
#define IN_B      32            /* second fire */
#define IN_START  64
#define IN_SELECT 128

void game_init(void);                           /* before the first tick: ROM pointers, the coroutine */
void game_tick(unsigned short input);           /* one NES frame (1/60 s) of game logic */
unsigned short game_bench_input(unsigned int tick);
uint32_t game_checksum(void);

extern uint8_t logic_extra_pad;                 /* NES buttons from the keyboard, ORed into every tick */
extern uint32_t logic_frames;                   /* NES frames (NMIs) run so far */
#endif
