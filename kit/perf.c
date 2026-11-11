/* perf.c - frame-time bookkeeping: proves (or disproves) that the game holds 50 Hz. */
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "perf.h"

ULONG perf_frames, perf_late, perf_worst, perf_total;
static char note[128];

void perf_reset(void) { perf_frames = perf_late = perf_worst = perf_total = 0; }

void perf_note(const char *text) { strncpy(note, text, sizeof note - 1); }

void perf_frame_start(ULONG vblanks)
{
    perf_frames++;
    if (vblanks > 1) perf_late += vblanks - 1;
}

void perf_frame_end(ULONG work_cck)
{
    perf_total += work_cck;
    if (work_cck > perf_worst) perf_worst = work_cck;
}

void perf_save(const char *path)
{
    char buf[320];
    ULONG budget = (ULONG)sys_lines * SYS_CCK_PER_LINE;
    ULONG avg = perf_frames ? perf_total / perf_frames : 0;
    ULONG hz = sys_is_pal ? 50 : 60;
    ULONG fps10 = perf_frames ? hz * 10 * perf_frames / (perf_frames + perf_late) : 0;
    BPTR fh;
    sprintf(buf, "frames=%lu late=%lu worst_cck=%lu avg_cck=%lu budget_cck=%lu worst_pct=%lu avg_pct=%lu display_hz=%lu shown_fps=%lu.%lu\n",
            (unsigned long)perf_frames, (unsigned long)perf_late, (unsigned long)perf_worst, (unsigned long)avg,
            (unsigned long)budget, (unsigned long)(perf_worst * 100 / budget), (unsigned long)(avg * 100 / budget),
            (unsigned long)hz, (unsigned long)(fps10 / 10), (unsigned long)(fps10 % 10));
    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (fh) {
        Write(fh, buf, strlen(buf));
        if (note[0]) { Write(fh, note, strlen(note)); Write(fh, "\n", 1); }
        Close(fh);
    }
}
