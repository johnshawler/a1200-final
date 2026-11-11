/* disp.h - AGA display for games: fixed or hardware-scrolling playfield, tiles, blitter objects, sprites.
 * Everything here was run on an emulated stock A1200 (see the brief). Bitplanes are NOT interleaved:
 * plane p, line y, world pixel x lives at  buffer + p*disp_plane + y*disp_row + ((x + disp_margin) >> 3).
 *
 * Scrolling screens: a buffer row is 512 pixels wide and the picture slides through memory as x grows,
 * so world (x, y) and (x + 512, y - 1) are the same memory. Draw tiles and objects only within 64 pixels
 * of the view, draw every tile of a new column (empty ones too), and keep x in 0 .. level_w - 320.
 * One buffer takes planes * (64 * buf_h + level_w / 8 + 64) bytes; a fixed screen planes * 40 * buf_h.
 */
#ifndef DISP_H
#define DISP_H
#include <exec/types.h>

typedef struct {
    UWORD planes;       /* 1..8 bitplanes = 2..256 colours. Fewer planes = faster drawing */
    UWORD scroll;       /* 0 = fixed 320-pixel screen, all 8 sprites.  1 = hardware scrolling */
    UWORD fetch;        /* 3 = 64-bit bitplane fetch: most CPU time; when scrolling only sprite 0 works
                           1 = 32-bit fetch: when scrolling sprites 0..4 work
                           0 = 16-bit fetch: when scrolling sprites 0..6 work; use only with 4 planes or fewer */
    UWORD view_h;       /* visible lines, up to 256 */
    UWORD buf_h;        /* buffer lines, >= view_h (view_h / 2 with double_y); more = room to scroll vertically */
    ULONG level_w;      /* scroll only: width of the level in pixels (how far x can go, plus 320) */
    UWORD buffers;      /* 1 = every call draws on the visible picture, 2 = front + back,
                           3 = front + back + clean: needed for disp_bob (with fewer, objects are never erased) */
    UWORD double_y;     /* 1 = every buffer line is shown twice (for half-height chunky screens) */
    UWORD sprite_w;     /* 16, 32 or 64 pixels: all sprites share one width */
    UWORD sprite_bank;  /* sprites take their colours from bank*16 .. bank*16+15 (1 = colours 16..31) */
} DispCfg;

extern UWORD  disp_row;         /* bytes per line of one bitplane */
extern ULONG  disp_plane;       /* bytes from one bitplane to the next */
extern UWORD  disp_margin;      /* hidden pixels left of world x = 0 (64 when scrolling, else 0) */
extern UBYTE *disp_front;       /* buffer on screen */
extern UBYTE *disp_back;        /* buffer to draw the next frame into */
extern UBYTE *disp_clean;       /* background without objects (buffers = 3) */

int   disp_open(const DispCfg *cfg);    /* call BEFORE sys_take(). 0 = not enough memory */
void  disp_close(void);                 /* call AFTER sys_give() */
void  disp_on(void);                    /* call AFTER sys_take(): start the display */
void  disp_palette(const ULONG *rgb, UWORD first, UWORD count);   /* entries are 0x00RRGGBB */

void  disp_scroll(LONG x, LONG y);      /* world pixel to show at the top-left corner, from the next disp_flip() */
void  disp_begin(void);                 /* start of a frame: erase the objects drawn on the back buffer earlier */
void  disp_flip(void);                  /* end of a frame: wait for the vertical blank, show the back buffer.
                                           Call it once per drawn frame; to wait without drawing use sys_wait_frame() */

/* Background: drawn into every buffer, all 256 pixels (colour 0 is drawn too, it is not see-through).
   tile = planes x 16 words (plane 0 rows first), in CHIP RAM. x must be a multiple of 16. */
void  disp_tile(LONG x, LONG y, const UWORD *tile);
/* Object: drawn into the back buffer through a mask, erased by the next-but-one disp_begin().
   image = planes x h x wwords words, mask = h x wwords words (1 = object pixel). Both in CHIP RAM.
   Call it in EVERY frame for every object that should be visible, also when the object stands still.
   At most 48 calls per frame; more are not drawn. Clipped at all four edges of a fixed screen. On a
   scrolling screen only top and bottom are clipped: keep objects within 64 pixels of the view. */
void  disp_bob(LONG x, LONG y, const UWORD *image, const UWORD *mask, UWORD wwords, UWORD h);
/* Background: draw a masked image into EVERY buffer, to stay there (a brick, a board piece, a letter).
   Same data as disp_bob. Not erased by disp_begin(). */
void  disp_stamp(LONG x, LONG y, const UWORD *image, const UWORD *mask, UWORD wwords, UWORD h);
/* Background, fixed screens only: copy a rectangle of a full picture into every buffer (also used to erase a
   stamp). picture = planes x buf_h x 40 bytes, plane 0 first, in CHIP RAM: what tools/gfx.py picture() writes
   for a 320-pixel-wide image. x and w must be multiples of 16. */
void  disp_paste(const UBYTE *picture, LONG x, LONG y, UWORD w, UWORD h);
/* Rectangles in one buffer (x and w multiples of 16) */
void  disp_clear(UBYTE *buf, LONG x, LONG y, UWORD w, UWORD h);

/* Hardware sprites. data = sprite block in CHIP RAM made by tools/gfx.py (2 control slots, h lines, end marker).
   disp_sprite() writes the position into the block: every sprite NUMBER that is on screen at the same time
   needs its OWN copy of the block (sys_chip_copy() makes one, correctly aligned for 32- and 64-pixel sprites).
   x, y are SCREEN pixels (not world). attach = 1 for the odd sprite of a 15-colour pair; give both sprites of
   a pair the same x, y and h. A sprite cannot start above display line 26: with view_h 256 it is switched
   off when y < -18, so let tall sprites leave sideways or downwards. On a scrolling screen only sprite
   numbers 0 (fetch 3), 0..4 (fetch 1) or 0..6 (fetch 0) exist; calls for the others are ignored. */
void  disp_sprite(UWORD n, UWORD *data, WORD x, WORD y, UWORD h, UWORD attach);
void  disp_sprite_off(UWORD n);

void  blit_wait(void);                  /* wait until the blitter is idle */

#endif
