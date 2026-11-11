/* host_test.c - milestone 3/8 check: run the game logic on the host for N ticks of the bench input and print
 * the game-state checksum. Build: cc -Isrc src/host_test.c src/logic.c src/nes.c src/coro_host.c src/gen_*.c
 *   host_test N            print the checksum after N ticks
 *   host_test N -v         also print the checksum every 100 ticks (to find the first tick that differs)
 *   host_test N -r file    also write NES RAM after every tick to file (for tools/verify)
 *   host_test N -d t1,t2   also write the picture after these ticks as host_NNNNN.ppm (host_render.c)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"
#include "logic.h"
void host_render(const char *path);

uint8_t nes_prg_data[0x20000], nes_chr_data[0x20000];

static void load(const char *name, uint8_t *dst)
{
    FILE *f = fopen(name, "rb");
    if (!f || fread(dst, 1, 0x20000, f) != 0x20000) { fprintf(stderr, "cannot read %s (run tools/make_assets.sh)\n", name); exit(2); }
    fclose(f);
}

int main(int argc, char **argv)
{
    unsigned n = argc > 1 ? (unsigned)atoi(argv[1]) : 3000, t;
    int verbose = argc > 2 && !strcmp(argv[2], "-v");
    FILE *ramlog = (argc > 3 && !strcmp(argv[2], "-r")) ? fopen(argv[3], "wb") : 0;
    const char *dumps = (argc > 3 && !strcmp(argv[2], "-d")) ? argv[3] : 0;   /* -d 100,200,... : frames as PPM */
    char key[32], name[64];
    load("assets/prg.bin", nes_prg_data);
    load("assets/chr.bin", nes_chr_data);
    game_init();
    for (t = 0; t < n; t++) {
        game_tick(game_bench_input(t));
        if (verbose && (t + 1) % 100 == 0) printf("tick %u checksum %08x frames %u\n", t + 1, (unsigned)game_checksum(), (unsigned)logic_frames);
        if (ramlog) fwrite(nes_ram, 1, sizeof nes_ram, ramlog);
        snprintf(key, sizeof key, ",%u,", t + 1);
        if (dumps) { char list[4096]; snprintf(list, sizeof list, ",%s,", dumps); if (strstr(list, key)) { snprintf(name, sizeof name, "host_%05u.ppm", t + 1); host_render(name); } }
    }
    if (nes_fatal_why) fprintf(stderr, "STOPPED: reason %d at %04x\n", nes_fatal_why, nes_fatal_pc);
    if (ramlog) fclose(ramlog);
    printf("ticks=%u checksum=%08x\n", n, (unsigned)game_checksum());
    return 0;
}
