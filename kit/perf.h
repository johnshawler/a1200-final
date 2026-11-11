/* perf.h - frame-time bookkeeping: proves (or disproves) that the game holds 50 Hz. */
#ifndef PERF_H
#define PERF_H
#include <exec/types.h>

void perf_reset(void);
void perf_frame_start(ULONG vblanks);             /* vertical blanks since the previous pass: 1 = on time */
void perf_frame_end(ULONG work_cck);              /* colour clocks this pass spent on logic + drawing */
void perf_note(const char *text);                 /* extra text for the report, e.g. "checksum=1234abcd" */
void perf_save(const char *path);                 /* call AFTER sys_give(): writes the report file */

extern ULONG perf_frames, perf_late, perf_worst, perf_total;

#endif
