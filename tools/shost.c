/* shost.c - host only: runs the game logic and src/sound.c with a simulated Paula and writes the sound as a
 * WAV file (22050 Hz stereo, Paula's channels 0+3 left, 1+2 right), to check the NES sound without the emulator.
 *   cc -Itools/hostinc -Ikit -Isrc -DPAULA='(&paula)' tools/shost.c src/sound.c src/logic.c src/nes.c src/run_c.c src/coro_host.c src/gen_*.c
 *   shost N out.wav       (N ticks of the bench input; one drawn frame per tick 6 in 5, as on a full-speed Amiga)
 *   INPUT=script          play a tools/nesref input script instead ("first last BUTTONS" lines)
 *   STATS=1 / STATS=2     print the four channels once a second / every frame
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exec/types.h"
#include "hardware/custom.h"
#include "nes.h"
#include "logic.h"
#include "sound.h"

struct Custom paula;
uint8_t nes_prg_data[0x20000], nes_chr_data[0x20000];
void *sys_alloc(ULONG size, ULONG f) { (void)f; return calloc(1, size); }

static struct { unsigned a, b; unsigned short bits; } script[4096]; static int nscript = -1;
static unsigned short input_at(unsigned t)
{
    unsigned short r = 0; int i;
    if (nscript < 0) {
        FILE *f = getenv("INPUT") ? fopen(getenv("INPUT"), "r") : 0; char l[256], *p, *tok; unsigned a, b;
        nscript = f ? 0 : -2;
        while (f && fgets(l, sizeof l, f) && nscript < 4096) {
            if (sscanf(l, "%u %u", &a, &b) != 2) continue;
            p = l; strtol(p, &p, 10); strtol(p, &p, 10);
            script[nscript].a = a; script[nscript].b = b; script[nscript].bits = 0;
            for (tok = strtok(p, " \t\r\n+"); tok; tok = strtok(0, " \t\r\n+"))
                script[nscript].bits |= !strcmp(tok, "A") ? IN_A : !strcmp(tok, "B") ? IN_B : !strcmp(tok, "START") ? IN_START :
                    !strcmp(tok, "SELECT") ? IN_SELECT : !strcmp(tok, "UP") ? IN_UP : !strcmp(tok, "DOWN") ? IN_DOWN :
                    !strcmp(tok, "LEFT") ? IN_LEFT : !strcmp(tok, "RIGHT") ? IN_RIGHT : 0;
            nscript++;
        }
        if (f) fclose(f);
    }
    if (nscript == -2) return game_bench_input(t);
    for (i = 0; i < nscript; i++) if (t >= script[i].a && t <= script[i].b) r |= script[i].bits;
    return r;
}

int main(int argc, char **argv)
{
    unsigned n = atoi(argv[1]), t, frame = 0; FILE *g, *w; double pos[4] = {0}; long samples = 0; int c, i;
    g = fopen("assets/prg.bin", "rb"); if (!g || fread(nes_prg_data, 1, 0x20000, g) != 0x20000) return 2; fclose(g);
    g = fopen("assets/chr.bin", "rb"); if (!g || fread(nes_chr_data, 1, 0x20000, g) != 0x20000) return 2; fclose(g);
    w = fopen(argv[2], "wb"); fseek(w, 44, SEEK_SET);
    game_init(); sound_load(); sound_start();
    for (t = 0; t < n; ) {
        int k = (frame % 5 == 4) ? 2 : 1;                       /* 60 ticks in 50 frames */
        while (k-- && t < n) { game_tick(input_at(t)); t++; }
        sound_frame(); frame++;
        if (getenv("STATS") && (frame % 50 == 0 || atoi(getenv("STATS")) == 2)) printf("t%5u %3us vol %2d %2d %2d %2d per %4d %4d %4d %4d  apu %02x\n", t, frame / 50, paula.aud[0].ac_vol, paula.aud[1].ac_vol, paula.aud[2].ac_vol, paula.aud[3].ac_vol, paula.aud[0].ac_per, paula.aud[1].ac_per, paula.aud[2].ac_per, paula.aud[3].ac_per, apu_reg[0x15]);
        for (i = 0; i < 441; i++) {                             /* 1/50 s at 22050 Hz */
            int lr[2] = {0, 0};
            for (c = 0; c < 4; c++) {
                struct AudChannel *a = &paula.aud[c];
                int len = a->ac_len * 2, v;
                if (!a->ac_ptr || !len || !a->ac_per) continue;
                v = ((signed char *)a->ac_ptr)[(int)pos[c] % len] * (a->ac_vol > 64 ? 64 : a->ac_vol);
                lr[(c == 0 || c == 3) ? 0 : 1] += v;
                pos[c] += 3546895.0 / a->ac_per / 22050.0;
                if (pos[c] >= len * 1000.0) pos[c] -= len * 1000.0;
            }
            for (c = 0; c < 2; c++) { int s = lr[c] * 2; short o = (short)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); fputc(o & 255, w); fputc((o >> 8) & 255, w); }
            samples++;
        }
    }
    {   unsigned data = (unsigned)(samples * 4), rate = 22050; unsigned char h[44] = "RIFF____WAVEfmt \x10\0\0\0\1\0\2\0________\4\0\x10\0data____";
        unsigned v[4] = { data + 36, rate, rate * 4, data }; int off[4] = { 4, 24, 28, 40 };
        for (c = 0; c < 4; c++) for (i = 0; i < 4; i++) h[off[c] + i] = (unsigned char)(v[c] >> (8 * i));
        fseek(w, 0, SEEK_SET); fwrite(h, 1, 44, w); fclose(w); }
    printf("%ld samples\n", samples);
    return 0;
}
