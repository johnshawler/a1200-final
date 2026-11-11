/* pcfb.h - the tiny "PC platform" layer these games were written against.
 * 8-bit indexed framebuffer, 60 Hz page flip, keyboard, file loading, sound triggers.
 */
#ifndef PCFB_H
#define PCFB_H
#include <stdint.h>

typedef struct { uint8_t r, g, b; } fb_color;

enum { FBK_LEFT, FBK_RIGHT, FBK_UP, FBK_DOWN, FBK_FIRE, FBK_QUIT, FBK_COUNT };

int      fb_open(int width, int height, const char *title);
uint8_t *fb_pixels(void);                               /* width*height bytes, row by row */
void     fb_palette(const fb_color *pal, int first, int count);
void     fb_flip(void);                                 /* show the frame, wait for 60 Hz vsync */
int      fb_key(int key);                               /* 1 while the key is held */
double   fb_time(void);                                 /* seconds since fb_open */
void    *fb_load(const char *path, long *size);         /* malloc + read a whole file, NULL on error */
void     fb_sound(int id);                              /* trigger sound effect number id */
void     fb_close(void);

#endif
