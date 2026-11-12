/* verify.c - host only: checks the translation. Interprets the original 6502 code instruction by instruction,
 * with the same frame model as src/logic.c (a frame ends at a wait loop listed in src/gen_waits.txt, the NMI
 * runs at the start of the next one) and the same machine (src/nes.c). Writes NES RAM after every tick, like
 * "host_test N -r file", so the two files can be compared byte for byte.
 *
 *   cc -O2 -Isrc tools/nesref/verify.c src/nes.c -o verify && ./verify N ram_interp.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nes.h"

uint8_t nes_prg_data[0x20000], nes_chr_data[0x20000];
void nes_wait(int kind, uint16_t pc) { (void)kind; (void)pc; }
uint16_t nes_dispatch(uint16_t pc) { return pc; }
uint16_t nes_reset_entry(void) { return 0; }

static uint8_t A, X, Y, S = 0xfd, P = 0x24; static uint16_t PC, pc0;
static struct { int bank; uint16_t br, loop; int kind; } waits[64]; static int nwaits;
static int s0at[0x10000];

static int bank_of(uint16_t a) { return a >= 0xc000 ? 7 : nes_bank; }
static uint8_t rd(uint16_t a)
{
    if (a == 0x2002 && s0at[pc0] && s0at[pc0] - 1 == bank_of(pc0)) return nes_read_s0();
    return nes_rd(a);
}
static void wr(uint16_t a, uint8_t v) { nes_wr(a, v); }
#define FC 1
#define FZ 2
#define FI 4
#define FD 8
#define FV 0x40
#define FN 0x80
static void setnz(uint8_t r) { P = (uint8_t)((P & ~(FN | FZ)) | (r & FN) | (r ? 0 : FZ)); }
static void push(uint8_t d) { nes_ram[0x100 | S--] = d; }
static uint8_t pop(void) { return nes_ram[0x100 | ++S]; }
static void adc(uint8_t m) { int r = A + m + (P & FC); P &= ~(FC | FV); if (r > 0xff) P |= FC; if (~(A ^ m) & (A ^ r) & 0x80) P |= FV; A = (uint8_t)r; setnz(A); }
static void cmp(uint8_t r, uint8_t m) { P = (uint8_t)((P & ~FC) | (r >= m ? FC : 0)); setnz((uint8_t)(r - m)); }

/* returns 1 when a wait loop's branch back was taken: the frame is over */
static int step(void)
{
    uint8_t op, m, lo, hi; uint16_t a = 0; int mode = 0, k;
    pc0 = PC;
    op = rd(PC++);
    switch (op & 0x1f) {
    case 0x00: case 0x02: mode = (op == 0x20) ? 4 : (op & 0x80) ? 1 : 0; break;
    case 0x09: mode = 1; break;
    case 0x01: mode = 2; break;
    case 0x04: case 0x05: case 0x06: mode = 3; break;
    case 0x0c: case 0x0d: case 0x0e: mode = 4; break;
    case 0x11: mode = 5; break;
    case 0x14: case 0x15: case 0x16: mode = 6; break;
    case 0x19: mode = 7; break;
    case 0x1c: case 0x1d: case 0x1e: mode = 8; break;
    }
    if (op == 0x96 || op == 0xb6) mode = 9;
    if (op == 0xbe) mode = 7;
    switch (mode) {
    case 1: a = PC++; break;
    case 2: m = (uint8_t)(rd(PC++) + X); a = (uint16_t)(nes_ram[m] | (nes_ram[(uint8_t)(m + 1)] << 8)); break;
    case 3: a = rd(PC++); break;
    case 4: lo = rd(PC++); hi = rd(PC++); a = (uint16_t)(lo | (hi << 8)); break;
    case 5: m = rd(PC++); a = (uint16_t)((nes_ram[m] | (nes_ram[(uint8_t)(m + 1)] << 8)) + Y); break;
    case 6: a = (uint8_t)(rd(PC++) + X); break;
    case 7: lo = rd(PC++); hi = rd(PC++); a = (uint16_t)((lo | (hi << 8)) + Y); break;
    case 8: lo = rd(PC++); hi = rd(PC++); a = (uint16_t)((lo | (hi << 8)) + X); break;
    case 9: a = (uint8_t)(rd(PC++) + Y); break;
    }
    switch (op) {
    case 0xa9: case 0xa5: case 0xb5: case 0xad: case 0xbd: case 0xb9: case 0xa1: case 0xb1: A = rd(a); setnz(A); break;
    case 0xa2: case 0xa6: case 0xb6: case 0xae: case 0xbe: X = rd(a); setnz(X); break;
    case 0xa0: case 0xa4: case 0xb4: case 0xac: case 0xbc: Y = rd(a); setnz(Y); break;
    case 0x85: case 0x95: case 0x8d: case 0x9d: case 0x99: case 0x81: case 0x91: wr(a, A); break;
    case 0x86: case 0x96: case 0x8e: wr(a, X); break;
    case 0x84: case 0x94: case 0x8c: wr(a, Y); break;
    case 0xaa: X = A; setnz(X); break; case 0xa8: Y = A; setnz(Y); break;
    case 0x8a: A = X; setnz(A); break; case 0x98: A = Y; setnz(A); break;
    case 0xba: X = S; setnz(X); break; case 0x9a: S = X; break;
    case 0x09: case 0x05: case 0x15: case 0x0d: case 0x1d: case 0x19: case 0x01: case 0x11: A |= rd(a); setnz(A); break;
    case 0x29: case 0x25: case 0x35: case 0x2d: case 0x3d: case 0x39: case 0x21: case 0x31: A &= rd(a); setnz(A); break;
    case 0x49: case 0x45: case 0x55: case 0x4d: case 0x5d: case 0x59: case 0x41: case 0x51: A ^= rd(a); setnz(A); break;
    case 0x69: case 0x65: case 0x75: case 0x6d: case 0x7d: case 0x79: case 0x61: case 0x71: adc(rd(a)); break;
    case 0xe9: case 0xe5: case 0xf5: case 0xed: case 0xfd: case 0xf9: case 0xe1: case 0xf1: adc((uint8_t)~rd(a)); break;
    case 0xc9: case 0xc5: case 0xd5: case 0xcd: case 0xdd: case 0xd9: case 0xc1: case 0xd1: cmp(A, rd(a)); break;
    case 0xe0: case 0xe4: case 0xec: cmp(X, rd(a)); break;
    case 0xc0: case 0xc4: case 0xcc: cmp(Y, rd(a)); break;
    case 0x24: case 0x2c: m = rd(a); P = (uint8_t)((P & ~(FN | FV | FZ)) | (m & (FN | FV)) | ((A & m) ? 0 : FZ)); break;
    case 0x0a: P = (uint8_t)((P & ~FC) | (A >> 7)); A <<= 1; setnz(A); break;
    case 0x4a: P = (uint8_t)((P & ~FC) | (A & 1)); A >>= 1; setnz(A); break;
    case 0x2a: { uint8_t c = P & FC; P = (uint8_t)((P & ~FC) | (A >> 7)); A = (uint8_t)((A << 1) | c); setnz(A); } break;
    case 0x6a: { uint8_t c = P & FC; P = (uint8_t)((P & ~FC) | (A & 1)); A = (uint8_t)((A >> 1) | (c << 7)); setnz(A); } break;
    case 0x06: case 0x16: case 0x0e: case 0x1e: m = rd(a); P = (uint8_t)((P & ~FC) | (m >> 7)); m <<= 1; wr(a, m); setnz(m); break;
    case 0x46: case 0x56: case 0x4e: case 0x5e: m = rd(a); P = (uint8_t)((P & ~FC) | (m & 1)); m >>= 1; wr(a, m); setnz(m); break;
    case 0x26: case 0x36: case 0x2e: case 0x3e: { uint8_t c = P & FC; m = rd(a); P = (uint8_t)((P & ~FC) | (m >> 7)); m = (uint8_t)((m << 1) | c); wr(a, m); setnz(m); } break;
    case 0x66: case 0x76: case 0x6e: case 0x7e: { uint8_t c = P & FC; m = rd(a); P = (uint8_t)((P & ~FC) | (m & 1)); m = (uint8_t)((m >> 1) | (c << 7)); wr(a, m); setnz(m); } break;
    case 0xe6: case 0xf6: case 0xee: case 0xfe: m = (uint8_t)(rd(a) + 1); wr(a, m); setnz(m); break;
    case 0xc6: case 0xd6: case 0xce: case 0xde: m = (uint8_t)(rd(a) - 1); wr(a, m); setnz(m); break;
    case 0xe8: X++; setnz(X); break; case 0xca: X--; setnz(X); break;
    case 0xc8: Y++; setnz(Y); break; case 0x88: Y--; setnz(Y); break;
    case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xb0: case 0xd0: case 0xf0: {
        int8_t off = (int8_t)rd(PC++); int f;
        switch (op >> 6) { case 0: f = P & FN; break; case 1: f = P & FV; break; case 2: f = P & FC; break; default: f = P & FZ; }
        if (((op >> 5) & 1) ? f : !f) {
            PC = (uint16_t)(PC + off);
            for (k = 0; k < nwaits; k++) if (waits[k].br == pc0 && waits[k].bank == bank_of(pc0)) return 1 + waits[k].kind;
        }
    } break;
    case 0x4c: PC = a; break;
    case 0x6c: PC = (uint16_t)(rd(a) | (rd((uint16_t)((a & 0xff00) | ((a + 1) & 0xff))) << 8)); break;
    case 0x20: push((uint8_t)((PC - 1) >> 8)); push((uint8_t)(PC - 1)); PC = a; break;
    case 0x60: PC = (uint16_t)((pop() | (pop() << 8)) + 1); break;
    case 0x40: P = (uint8_t)((pop() & ~0x10) | 0x20); PC = (uint16_t)(pop() | (pop() << 8)); break;
    case 0x48: push(A); break; case 0x68: A = pop(); setnz(A); break;
    case 0x08: push(P | 0x30); break; case 0x28: P = (uint8_t)((pop() & ~0x10) | 0x20); break;
    case 0x18: P &= ~FC; break; case 0x38: P |= FC; break; case 0x58: P &= ~FI; break; case 0x78: P |= FI; break;
    case 0xb8: P &= ~FV; break; case 0xd8: P &= ~FD; break; case 0xf8: P |= FD; break;
    case 0xea: break;
    default: fprintf(stderr, "verify: unknown opcode %02x at %04x bank %d\n", op, pc0, bank_of(pc0)); exit(3);
    }
    return 0;
}

static unsigned short bench_input(unsigned int t)      /* copy of game_bench_input() in src/logic.c */
{
    /* BENCH INPUT BEGIN (per game: tools/new_port.sh resets it) */
    if (t >= 100 && t <= 105) return 64;
    if (t >= 200 && t <= 205) return 4;
    if (t >= 250 && t <= 255) return 16;
    if (t >= 330 && t <= 335) return 4;
    if (t >= 360 && t <= 365) return 4;
    if (t >= 400 && t <= 405) return 16;
    if (t >= 500 && (t & 127) < 4) return 16;
    /* BENCH INPUT END */
    return 0;
}
static uint8_t pad_of(unsigned short in)
{
    uint8_t p = 0;
    if (in & 16) p |= 1; if (in & 32) p |= 2; if (in & 128) p |= 4; if (in & 64) p |= 8;
    if (in & 8) p |= 0x10; if (in & 4) p |= 0x20; if (in & 2) p |= 0x40; if (in & 1) p |= 0x80;
    return p;
}

int main(int argc, char **argv)
{
    unsigned n = (unsigned)atoi(argv[1]), t; FILE *log = fopen(argv[2], "wb"), *f; char line[128];
    int kind = -1;
    f = fopen("assets/prg.bin", "rb"); if (!f || fread(nes_prg_data, 1, 0x20000, f) != 0x20000) return 2; fclose(f);
    f = fopen("assets/chr.bin", "rb"); if (!f || fread(nes_chr_data, 1, 0x20000, f) != 0x20000) return 2; fclose(f);
    f = fopen("src/gen_waits.txt", "r");
    while (f && fgets(line, sizeof line, f)) {
        int b; unsigned br, lp; char k[8];
        if (sscanf(line, "%d %x %x %7s", &b, &br, &lp, k) != 4) continue;
        if (!strcmp(k, "S0")) { s0at[br] = b + 1; continue; }
        waits[nwaits].bank = b; waits[nwaits].br = (uint16_t)br; waits[nwaits].loop = (uint16_t)lp;
        waits[nwaits].kind = !strcmp(k, "VBL"); nwaits++;
    }
    if (f) fclose(f);
    nes_init(nes_prg_data, nes_chr_data);
    PC = (uint16_t)(nes_rd(0xfffc) | nes_rd(0xfffd) << 8);
    for (t = 0; t < n; t++) {
        nes_pad = pad_of(bench_input(t));
        if (kind >= 0) {                                /* start of a frame after a wait: NMI, then the vblank flag */
            if (ppu_ctrl & 0x80) {
                uint16_t back = PC; uint8_t s0 = S;
                push((uint8_t)(PC >> 8)); push((uint8_t)PC); push((uint8_t)((P & ~0x10) | 0x20)); P |= FI;
                PC = (uint16_t)(nes_rd(0xfffa) | nes_rd(0xfffb) << 8);
                split_seen = 0;
                while (!(PC == back && S == s0)) {
                    if (step()) { fprintf(stderr, "verify: wait inside the NMI at tick %u\n", t); return 3; }
                }
                split_on = split_seen;
            }
            if (kind == 1) ppu_vbl = 1;
        }
        for (;;) { int r = step(); if (r) { kind = r - 1; break; } }
        fwrite(nes_ram, 1, sizeof nes_ram, log);
    }
    fclose(log);
    return 0;
}
