/* sound.h - the NES sound on Paula (src/sound.c). */
#ifndef SOUND_H
#define SOUND_H
int  sound_load(void);                  /* before sys_take(): waveforms in chip RAM */
void sound_start(void);                 /* after sys_take() */
void sound_frame(void);                 /* once per drawn frame */
void sound_stop(void);
#endif
