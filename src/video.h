/* video.h - the NES picture on the AGA display (src/video.c). */
#ifndef VIDEO_H
#define VIDEO_H
#include "disp.h"
extern const DispCfg video_display;
extern int video_sprites_dropped;       /* NES sprite clusters that found no hardware sprite (diagnostics) */
int  video_load(void);                  /* before sys_take(): sprite lists in chip RAM */
void video_start(void);                 /* after disp_on() */
void video_frame(void);                 /* once per drawn frame */
#endif
