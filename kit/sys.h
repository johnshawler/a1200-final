/* sys.h - take over a stock Amiga 1200 from AmigaOS 3.x and give it back.
 * Build: m68k-amigaos-gcc -m68020 -O2 -fomit-frame-pointer -noixemul
 */
#ifndef SYS_H
#define SYS_H

#include <exec/types.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>

#define custom ((volatile struct Custom *)0xdff000)
#define ciaa   ((volatile struct CIA *)0xbfe001)
#define ciab   ((volatile struct CIA *)0xbfd000)

/* Geometry of one video frame, filled in by sys_take(). */
#define SYS_CCK_PER_LINE 227            /* colour clocks (bus slots) per scanline, PAL */
extern UWORD sys_lines;                 /* scanlines per frame: 313 PAL, 263 NTSC */
extern UWORD sys_is_pal;                /* 1 = 50 Hz, 0 = 60 Hz */
extern UWORD sys_is_aga;                /* 1 = AGA chipset present */

extern ULONG *sys_vbr;                  /* CPU vector base, valid after sys_take() */
extern volatile ULONG sys_frame;        /* +1 at every vertical blank (line 0) */
extern void (*volatile sys_vbl_hook)(void); /* optional: called from the interrupt each frame */

extern UWORD sys_force_pal;             /* set to 1 before sys_take(): switch an NTSC machine to 50 Hz while the game runs */
void *sys_load(const char *name, ULONG memflags, ULONG *size);   /* read a whole file into AllocMem'd memory; BEFORE sys_take() only */
void *sys_alloc(ULONG size, ULONG memflags);    /* AllocMem that sys_free_all() gives back; BEFORE sys_take() only */
void *sys_chip_copy(const void *src, ULONG size);   /* own, 8-byte aligned chip RAM copy of a block */
void  sys_free_all(void);                       /* call last, after sys_give() and disp_close() */
int   sys_take(void);                   /* 1 = machine is yours; 0 = failed (no Kickstart 3, or no AGA), nothing changed */
void  sys_give(void);                   /* restore everything sys_take() changed */

void  sys_wait_frame(void);             /* sleep until the next vertical blank */
ULONG sys_beam(void);                   /* colour clocks since line 0 of this frame */
ULONG sys_now(void);                    /* colour clocks, 1 unit = 281.9 ns PAL; wraps after 20 minutes: use differences only */

/* Input. Joystick bits. */
#define JOY_RIGHT 1
#define JOY_LEFT  2
#define JOY_DOWN  4
#define JOY_UP    8
#define JOY_FIRE  16
#define JOY_FIRE2 32
UWORD sys_joy(int port);                /* port 1 = joystick port, port 0 = mouse port */
int   sys_mouse_button(void);           /* left mouse button, 1 = down */

/* Keyboard. Set sys_vbl_hook = sys_key_poll; after sys_take() (the skeleton does): keys are then read at every
   vertical blank. Without the hook, call sys_key_poll() yourself at least 10 times a second. */
extern volatile UBYTE sys_keys[128];             /* 1 = key is down, indexed by raw key code */
void  sys_key_poll(void);
#define KEY_ESC   0x45
#define KEY_SPACE 0x40
#define KEY_RET   0x44
#define KEY_UP    0x4c
#define KEY_DOWN  0x4d
#define KEY_RIGHT 0x4e
#define KEY_LEFT  0x4f

#endif
