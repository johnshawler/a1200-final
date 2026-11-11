/* nesref.c - host-only reference NES (6502 + MMC1 + PPU) used to study the original game and to check the
 * port against it. It never runs on the Amiga.
 *
 *   nesref rom.nes FRAMES [options]
 *     -i input.txt      lines "first last BUTTONS" (A B SELECT START UP DOWN LEFT RIGHT), frames inclusive
 *     -d f1,f2,...      dump these frames as frame_NNNNN.ppm
 *     -o dir            where dumps go (default .)
 *     -c cov.txt        write code coverage: "bank pc" of every executed instruction + indirect targets
 *     -s                print per-frame stats (instructions, ppu writes)
 *     -t                trace every instruction (huge)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t *prg, *chr; static int prg_banks, chr_banks8;
static uint8_t ram[0x800], vram[0x800], pal[32], oam[256], chrram[0x2000];
/* MMC1 */
static uint8_t m_shift = 0x10, m_ctrl = 0x0c, m_chr0, m_chr1, m_prg;
static uint8_t prgram[0x2000];

/* CPU */
static uint8_t A, X, Y, S = 0xfd, P = 0x24; static uint16_t PC;
static long long cycles;
static int nmi_pending;
/* PPU */
static uint8_t ppuctrl, ppumask, ppustatus, oamaddr, rdbuf; static uint16_t v, t; static uint8_t fx, w;
static int scanline; static uint8_t fb[240][256];
/* input */
static uint8_t pad, padshift, strobe;
/* stats */
static long frame_instr, frame_ppuw, frame_2002r; static int stats, trace;
static uint8_t *cov; /* [bank*0x4000 + offs] for $8000-$BFFF, plus fixed bank */
static FILE *covf;
static long long instr_total;
static int prg_bank_for(uint16_t a);
static unsigned *prof; static FILE *proff;
static uint16_t shadow[256]; static int nshadow;
static void flow(char kind, uint16_t from, uint16_t to)
{
    static uint8_t *seen; long k;
    if (!covf) return;
    if (!seen) seen = calloc(4 * 16 * 0x10000, 1);
    k = ((long)(kind == 'U' ? 0 : kind == 'I' ? 1 : kind == 'X' ? 2 : 3) * 16 + (to >= 0x8000 ? prg_bank_for(to) : 15)) * 0x10000 + to;
    if (kind == 'U' || kind == 'X') { if (seen[k]) return; seen[k] = 1; }
    else { if (seen[k]) return; seen[k] = 1; }
    fprintf(covf, "%c %d %04x %04x %d\n", kind, to >= 0x8000 ? prg_bank_for(to) : -1, to, from, from >= 0x8000 ? prg_bank_for(from) : -1);
}

static int prg_bank_for(uint16_t a)
{
    int mode = (m_ctrl >> 2) & 3, b = m_prg & 0x0f;
    if (mode < 2) return (a < 0xc000) ? (b & 0xe) : (b | 1);
    if (mode == 2) return (a < 0xc000) ? 0 : b;
    return (a < 0xc000) ? b : prg_banks - 1;
}
static uint8_t *chr_ptr(uint16_t a)
{
    if (!chr_banks8) return &chrram[a & 0x1fff];
    if (m_ctrl & 0x10) {    /* 4K mode */
        int b = (a < 0x1000) ? m_chr0 : m_chr1;
        return &chr[((b % (chr_banks8 * 2)) * 0x1000) + (a & 0xfff)];
    }
    return &chr[(((m_chr0 >> 1) % chr_banks8) * 0x2000) + (a & 0x1fff)];
}
static uint16_t nt_addr(uint16_t a)
{
    a &= 0xfff;
    switch (m_ctrl & 3) {
    case 0: return a & 0x3ff;
    case 1: return 0x400 | (a & 0x3ff);
    case 2: return a & 0x7ff;                       /* vertical */
    default: return ((a >> 1) & 0x400) | (a & 0x3ff);   /* horizontal */
    }
}
static uint8_t ppu_read(uint16_t a)
{
    a &= 0x3fff;
    if (a < 0x2000) return *chr_ptr(a);
    if (a < 0x3f00) return vram[nt_addr(a)];
    a &= 0x1f; if ((a & 0x13) == 0x10) a &= 0x0f; return pal[a];
}
static void ppu_write(uint16_t a, uint8_t d)
{
    a &= 0x3fff;
    if (a < 0x2000) { if (!chr_banks8) chrram[a] = d; return; }
    if (a < 0x3f00) { vram[nt_addr(a)] = d; return; }
    a &= 0x1f; if ((a & 0x13) == 0x10) a &= 0x0f; pal[a] = d & 0x3f;
}

static uint8_t rd(uint16_t a)
{
    if (a < 0x2000) return ram[a & 0x7ff];
    if (a < 0x4000) {
        switch (a & 7) {
        case 2: { uint8_t r = ppustatus; ppustatus &= 0x7f; w = 0; frame_2002r++; return r; }
        case 4: return oam[oamaddr];
        case 7: { uint8_t r; if ((v & 0x3fff) >= 0x3f00) { r = ppu_read(v); rdbuf = ppu_read(v - 0x1000); }
                  else { r = rdbuf; rdbuf = ppu_read(v); }
                  v += (ppuctrl & 4) ? 32 : 1; return r; }
        }
        return 0;
    }
    if (a == 0x4016) { uint8_t r = padshift & 1; if (!strobe) padshift = (padshift >> 1) | 0x80; return r | 0x40; }
    if (a == 0x4017) return 0x40;
    if (a < 0x6000) return 0;
    if (a < 0x8000) return prgram[a & 0x1fff];
    return prg[prg_bank_for(a) * 0x4000 + (a & 0x3fff)];
}
static void wr(uint16_t a, uint8_t d)
{
    if (a < 0x2000) { ram[a & 0x7ff] = d; return; }
    if (a < 0x4000) {
        frame_ppuw++;
        if (trace) printf("  PPU %04x <- %02x  (line %d)\n", a & 0x2007, d, scanline);
        switch (a & 7) {
        case 0: ppuctrl = d; t = (t & 0xf3ff) | ((d & 3) << 10); break;
        case 1: ppumask = d; break;
        case 3: oamaddr = d; break;
        case 4: oam[oamaddr++] = d; break;
        case 5: if (!w) { t = (t & 0xffe0) | (d >> 3); fx = d & 7; } else t = (t & 0x8c1f) | ((d & 7) << 12) | ((d & 0xf8) << 2); w ^= 1; break;
        case 6: if (!w) t = (t & 0x00ff) | ((d & 0x3f) << 8); else { t = (t & 0xff00) | d; v = t; } w ^= 1; break;
        case 7: ppu_write(v, d); v += (ppuctrl & 4) ? 32 : 1; break;
        }
        return;
    }
    if (a == 0x4014) { int i; for (i = 0; i < 256; i++) oam[(oamaddr + i) & 255] = rd((d << 8) | i); cycles += 513; return; }
    if (a == 0x4016) { strobe = d & 1; if (strobe) padshift = pad; return; }
    if (a < 0x6000) return;
    if (a < 0x8000) { prgram[a & 0x1fff] = d; return; }
    /* MMC1 */
    if (d & 0x80) { m_shift = 0x10; m_ctrl |= 0x0c; return; }
    { int done = m_shift & 1; m_shift = (m_shift >> 1) | ((d & 1) << 4);
      if (done) { uint8_t val = m_shift; int reg = (a >> 13) & 3;
          if (reg == 0) m_ctrl = val; else if (reg == 1) m_chr0 = val; else if (reg == 2) m_chr1 = val; else m_prg = val;
          m_shift = 0x10; } }
}

static void cov_mark(uint16_t pc, int kind)
{
    int bank = pc >= 0x8000 ? prg_bank_for(pc) : -1;
    long idx;
    if (!cov) return;
    if (pc < 0x8000) { idx = 0; if (!(cov[prg_banks * 0x4000 + (pc & 0x7ff)] & 1)) fprintf(covf, "R %04x\n", pc); cov[prg_banks * 0x4000 + (pc & 0x7ff)] |= 1; return; }
    idx = bank * 0x4000 + (pc & 0x3fff);
    if (!(cov[idx] & kind)) { cov[idx] |= kind; if (kind != 1) fprintf(covf, "%c %d %04x\n", kind == 2 ? 'J' : kind == 4 ? 'S' : 'N', bank, pc); }
    (void)idx;
}

/* ---- 6502 ---- */
#define FZ 2
#define FC 1
#define FI 4
#define FD 8
#define FB 0x10
#define FV 0x40
#define FN 0x80
static void setnz(uint8_t r) { P = (P & ~(FN | FZ)) | (r & FN) | (r ? 0 : FZ); }
static void push(uint8_t d) { ram[0x100 | S--] = d; }
static uint8_t pop(void) { return ram[0x100 | ++S]; }
static void adc(uint8_t m) { int r = A + m + (P & FC); P &= ~(FC | FV); if (r > 0xff) P |= FC; if (~(A ^ m) & (A ^ r) & 0x80) P |= FV; A = r; setnz(A); }
static void cmp(uint8_t r, uint8_t m) { int d = r - m; P = (P & ~FC) | (r >= m ? FC : 0); setnz(d); }
static const uint8_t cyc[256] = {
7,6,2,8,3,3,5,5,3,2,2,2,4,4,6,6,2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
6,6,2,8,3,3,5,5,4,2,2,2,4,4,6,6,2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
6,6,2,8,3,3,5,5,3,2,2,2,3,4,6,6,2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
6,6,2,8,3,3,5,5,4,2,2,2,5,4,6,6,2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,2,6,2,6,4,4,4,4,2,5,2,5,5,5,5,5,
2,6,2,6,3,3,3,3,2,2,2,2,4,4,4,4,2,5,2,5,4,4,4,4,2,4,2,4,4,4,4,4,
2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7,
2,6,2,8,3,3,5,5,2,2,2,2,4,4,6,6,2,5,2,8,4,4,6,6,2,4,2,7,4,4,7,7};

static void irq_nmi(uint16_t vec)
{
    push(PC >> 8); push(PC & 0xff); push((P & ~FB) | 0x20); P |= FI;
    PC = rd(vec) | (rd(vec + 1) << 8); cycles += 7;
    cov_mark(PC, 4);
}

static void step(void)
{
    uint8_t op, m, lo, hi; uint16_t a = 0, pc0 = PC; int mode;
    if (nmi_pending == 1) { nmi_pending = 0; irq_nmi(0xfffa); pc0 = PC; } else if (nmi_pending) nmi_pending--;   /* NMI one instruction after the flag rises */
    if (cov) cov_mark(PC, 1);
    if (prof) prof[(PC >= 0x8000 ? prg_bank_for(PC) : 15) * 0x10000 + PC]++;
    op = rd(PC++);
    if (trace) printf("%02x:%04x %02x A=%02x X=%02x Y=%02x P=%02x S=%02x\n", pc0 >= 0x8000 ? prg_bank_for(pc0) : 0xff, pc0, op, A, X, Y, P, S);
    cycles += cyc[op]; instr_total++;
    { static int nw = -1; static unsigned short wl[64]; int k, waiting = 0;
      if (nw < 0) { char *e = getenv("WAITPCS"); nw = 0; while (e && *e && nw < 64) { wl[nw++] = strtol(e, &e, 16); if (*e) e++; } }
      for (k = 0; k < nw; k++) if (wl[k] == pc0) waiting = 1;
      if (!waiting) frame_instr++; }
    /* addressing */
    mode = 0;
    switch (op & 0x1f) {
    case 0x00: case 0x02: case 0x09: case 0x0b: mode = (op == 0x20) ? 9 : (op & 0x80) ? 1 : 0; break; /* imm for 80+ */
    case 0x01: case 0x03: mode = 2; break;                      /* (zp,x) */
    case 0x04: case 0x05: case 0x06: case 0x07: mode = 3; break; /* zp */
    case 0x0c: case 0x0d: case 0x0e: case 0x0f: mode = 4; break; /* abs */
    case 0x11: case 0x13: mode = 5; break;                      /* (zp),y */
    case 0x14: case 0x15: case 0x16: case 0x17: mode = 6; break; /* zp,x */
    case 0x19: case 0x1b: mode = 7; break;                      /* abs,y */
    case 0x1c: case 0x1d: case 0x1e: case 0x1f: mode = 8; break; /* abs,x */
    }
    if (op == 0x96 || op == 0xb6 || op == 0x97 || op == 0xb7) mode = 10;   /* zp,y */
    if (op == 0xbe || op == 0xbf) mode = 7;
    switch (mode) {
    case 1: a = PC++; break;
    case 2: m = rd(PC++) + X; a = ram[m] | (ram[(uint8_t)(m + 1)] << 8); break;
    case 3: a = rd(PC++); break;
    case 4: case 9: lo = rd(PC++); hi = rd(PC++); a = lo | (hi << 8); break;
    case 5: m = rd(PC++); a = (ram[m] | (ram[(uint8_t)(m + 1)] << 8)) + Y; break;
    case 6: a = (uint8_t)(rd(PC++) + X); break;
    case 7: lo = rd(PC++); hi = rd(PC++); a = (lo | (hi << 8)) + Y; break;
    case 8: lo = rd(PC++); hi = rd(PC++); a = (lo | (hi << 8)) + X; break;
    case 10: a = (uint8_t)(rd(PC++) + Y); break;
    }
    if ((op & 0x1f) == 0x10) a = PC + 1;   /* branches */
    if (op == 0x09 || op == 0x29 || op == 0x49 || op == 0x69) a = PC++;  /* imm 01-group */
    switch (op) {
    /* loads/stores */
    case 0xa9: case 0xa5: case 0xb5: case 0xad: case 0xbd: case 0xb9: case 0xa1: case 0xb1: A = rd(a); setnz(A); break;
    case 0xa2: case 0xa6: case 0xb6: case 0xae: case 0xbe: X = rd(a); setnz(X); break;
    case 0xa0: case 0xa4: case 0xb4: case 0xac: case 0xbc: Y = rd(a); setnz(Y); break;
    case 0x85: case 0x95: case 0x8d: case 0x9d: case 0x99: case 0x81: case 0x91: wr(a, A); break;
    case 0x86: case 0x96: case 0x8e: wr(a, X); break;
    case 0x84: case 0x94: case 0x8c: wr(a, Y); break;
    case 0xaa: X = A; setnz(X); break; case 0xa8: Y = A; setnz(Y); break;
    case 0x8a: A = X; setnz(A); break; case 0x98: A = Y; setnz(A); break;
    case 0xba: X = S; setnz(X); break; case 0x9a: S = X; break;
    /* alu */
    case 0x09: case 0x05: case 0x15: case 0x0d: case 0x1d: case 0x19: case 0x01: case 0x11: A |= rd(a); setnz(A); break;
    case 0x29: case 0x25: case 0x35: case 0x2d: case 0x3d: case 0x39: case 0x21: case 0x31: A &= rd(a); setnz(A); break;
    case 0x49: case 0x45: case 0x55: case 0x4d: case 0x5d: case 0x59: case 0x41: case 0x51: A ^= rd(a); setnz(A); break;
    case 0x69: case 0x65: case 0x75: case 0x6d: case 0x7d: case 0x79: case 0x61: case 0x71: adc(rd(a)); break;
    case 0xe9: case 0xe5: case 0xf5: case 0xed: case 0xfd: case 0xf9: case 0xe1: case 0xf1: adc(~rd(a)); break;
    case 0xc9: case 0xc5: case 0xd5: case 0xcd: case 0xdd: case 0xd9: case 0xc1: case 0xd1: cmp(A, rd(a)); break;
    case 0xe0: case 0xe4: case 0xec: cmp(X, rd(a)); break;
    case 0xc0: case 0xc4: case 0xcc: cmp(Y, rd(a)); break;
    case 0x24: case 0x2c: m = rd(a); P = (P & ~(FN | FV | FZ)) | (m & (FN | FV)) | ((A & m) ? 0 : FZ); break;
    /* rmw */
    case 0x0a: P = (P & ~FC) | (A >> 7); A <<= 1; setnz(A); break;
    case 0x4a: P = (P & ~FC) | (A & 1); A >>= 1; setnz(A); break;
    case 0x2a: { uint8_t c = P & FC; P = (P & ~FC) | (A >> 7); A = (A << 1) | c; setnz(A); } break;
    case 0x6a: { uint8_t c = P & FC; P = (P & ~FC) | (A & 1); A = (A >> 1) | (c << 7); setnz(A); } break;
    case 0x06: case 0x16: case 0x0e: case 0x1e: m = rd(a); P = (P & ~FC) | (m >> 7); m <<= 1; wr(a, m); setnz(m); break;
    case 0x46: case 0x56: case 0x4e: case 0x5e: m = rd(a); P = (P & ~FC) | (m & 1); m >>= 1; wr(a, m); setnz(m); break;
    case 0x26: case 0x36: case 0x2e: case 0x3e: { uint8_t c = P & FC; m = rd(a); P = (P & ~FC) | (m >> 7); m = (m << 1) | c; wr(a, m); setnz(m); } break;
    case 0x66: case 0x76: case 0x6e: case 0x7e: { uint8_t c = P & FC; m = rd(a); P = (P & ~FC) | (m & 1); m = (m >> 1) | (c << 7); wr(a, m); setnz(m); } break;
    case 0xe6: case 0xf6: case 0xee: case 0xfe: m = rd(a) + 1; wr(a, m); setnz(m); break;
    case 0xc6: case 0xd6: case 0xce: case 0xde: m = rd(a) - 1; wr(a, m); setnz(m); break;
    case 0xe8: X++; setnz(X); break; case 0xca: X--; setnz(X); break;
    case 0xc8: Y++; setnz(Y); break; case 0x88: Y--; setnz(Y); break;
    /* branches */
    case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xb0: case 0xd0: case 0xf0: {
        int8_t off = rd(PC++); int f;
        switch (op >> 6) { case 0: f = P & FN; break; case 1: f = P & FV; break; case 2: f = P & FC; break; default: f = P & FZ; }
        if (((op >> 5) & 1) ? f : !f) { PC += off; cycles++; }
    } break;
    case 0x4c: PC = a; if (PC >= 0x8000 && PC < 0xc000 && (pc0 >= 0xc000 || pc0 < 0x8000)) flow('X', pc0, PC); break;
    case 0x6c: PC = rd(a) | (rd((a & 0xff00) | ((a + 1) & 0xff)) << 8); flow('I', pc0, PC); break;
    case 0x20: push((PC - 1) >> 8); push((PC - 1) & 0xff); if (nshadow < 256) shadow[nshadow++] = PC; PC = a; if (PC >= 0x8000 && PC < 0xc000 && (pc0 >= 0xc000 || pc0 < 0x8000)) flow('X', pc0, PC); break;
    case 0x60: PC = (pop() | (pop() << 8)) + 1;
        if (nshadow && shadow[nshadow - 1] == PC) nshadow--;
        else { int k = nshadow; while (k && shadow[k - 1] != PC) k--; if (k) nshadow = k - 1; flow('U', pc0, PC); }
        break;
    case 0x40: P = (pop() & ~FB) | 0x20; PC = pop() | (pop() << 8); break;
    case 0x00: PC++; push(PC >> 8); push(PC & 0xff); push(P | FB | 0x20); P |= FI; PC = rd(0xfffe) | (rd(0xffff) << 8); break;
    case 0x48: push(A); break; case 0x68: A = pop(); setnz(A); break;
    case 0x08: push(P | FB | 0x20); break; case 0x28: P = (pop() & ~FB) | 0x20; break;
    case 0x18: P &= ~FC; break; case 0x38: P |= FC; break; case 0x58: P &= ~FI; break; case 0x78: P |= FI; break;
    case 0xb8: P &= ~FV; break; case 0xd8: P &= ~FD; break; case 0xf8: P |= FD; break;
    case 0xea: break;
    default: fprintf(stderr, "illegal opcode %02x at %04x (bank %d)\n", op, pc0, prg_bank_for(pc0)); exit(3);
    }
}

/* ---- PPU rendering, one scanline at a time ---- */
static void render_line(int y)
{
    int x; uint8_t line[256], bgop[256];
    memset(line, 0, 256); memset(bgop, 0, 256);
    if (ppumask & 0x08) {
        uint16_t vv = v; int fine_y = (vv >> 12) & 7;
        for (x = 0; x < 256 + 8; x += 8) {
            uint16_t ntaddr = 0x2000 | (vv & 0x0fff);
            uint8_t tile = ppu_read(ntaddr);
            uint16_t at = 0x23c0 | (vv & 0x0c00) | ((vv >> 4) & 0x38) | ((vv >> 2) & 7);
            uint8_t attr = (ppu_read(at) >> (((vv >> 4) & 4) | (vv & 2))) & 3;
            uint16_t pa = ((ppuctrl & 0x10) ? 0x1000 : 0) + tile * 16 + fine_y;
            uint8_t p0 = ppu_read(pa), p1 = ppu_read(pa + 8); int b;
            for (b = 0; b < 8; b++) {
                int px = x + b - fx; uint8_t c = ((p0 >> (7 - b)) & 1) | (((p1 >> (7 - b)) & 1) << 1);
                if (px >= 0 && px < 256) { bgop[px] = c; line[px] = c ? (attr << 2) | c : 0; }
            }
            if ((vv & 0x1f) == 31) { vv &= ~0x1f; vv ^= 0x400; } else vv++;
        }
        if (!(ppumask & 2)) for (x = 0; x < 8; x++) { line[x] = 0; bgop[x] = 0; }
    }
    if (ppumask & 0x10) {
        int i, n = 0, h = (ppuctrl & 0x20) ? 16 : 8;
        uint8_t sl[256]; int prio[256]; memset(sl, 0, 256); for (x = 0; x < 256; x++) prio[x] = -1;
        for (i = 0; i < 64; i++) {
            int sy = oam[i * 4] + 1, row = y - sy, tile = oam[i * 4 + 1], at = oam[i * 4 + 2], sx = oam[i * 4 + 3];
            uint16_t pa; uint8_t p0, p1; int b;
            if (row < 0 || row >= h) continue;
            if (++n > 8) break;
            if (at & 0x80) row = h - 1 - row;
            if (h == 16) pa = ((tile & 1) << 12) + (tile & 0xfe) * 16 + (row >= 8 ? 16 : 0) + (row & 7);
            else pa = ((ppuctrl & 0x08) ? 0x1000 : 0) + tile * 16 + row;
            p0 = ppu_read(pa); p1 = ppu_read(pa + 8);
            for (b = 0; b < 8; b++) {
                int bit = (at & 0x40) ? b : 7 - b, px = sx + b; uint8_t c = ((p0 >> bit) & 1) | (((p1 >> bit) & 1) << 1);
                if (px > 255 || !c || prio[px] >= 0) continue;
                if (px < 8 && !(ppumask & 4)) continue;
                if (i == 0 && bgop[px] && px != 255) ppustatus |= 0x40;
                prio[px] = i; sl[px] = 0x10 | ((at & 3) << 2) | c;
                if (!(at & 0x20) || !bgop[px]) line[px] = sl[px]; else line[px] = line[px];
            }
        }
    }
    for (x = 0; x < 256; x++) fb[y][x] = (line[x] & 3) ? pal[line[x] & 0x1f] : pal[0];
}

static const uint32_t nespal[64] = {
0x666666,0x002a88,0x1412a7,0x3b00a4,0x5c007e,0x6e0040,0x6c0600,0x561d00,0x333500,0x0b4800,0x005200,0x004f08,0x00404d,0,0,0,
0xadadad,0x155fd9,0x4240ff,0x7527fe,0xa01acc,0xb71e7b,0xb53120,0x994e00,0x6b6d00,0x388700,0x0c9300,0x008f32,0x007c8d,0,0,0,
0xfffeff,0x64b0ff,0x9290ff,0xc676ff,0xf36aff,0xfe6ecc,0xfe8170,0xea9e22,0xbcbe00,0x88d800,0x5ce430,0x45e082,0x48cdde,0x4f4f4f,0,0,
0xfffeff,0xc0dfff,0xd3d2ff,0xe8c8ff,0xfbc2ff,0xfec4ea,0xfeccc5,0xf7d8a5,0xe4e594,0xcfef96,0xbdf4ab,0xb3f3cc,0xb5ebf2,0xb8b8b8,0,0};

static void dump(const char *dir, int f)
{
    char name[512]; FILE *o; int x, y;
    snprintf(name, sizeof name, "%s/frame_%05d.ppm", dir, f);
    o = fopen(name, "wb"); if (!o) return;
    fprintf(o, "P6\n256 240\n255\n");
    for (y = 0; y < 240; y++) for (x = 0; x < 256; x++) { uint32_t c = nespal[fb[y][x] & 0x3f]; fputc(c >> 16, o); fputc((c >> 8) & 255, o); fputc(c & 255, o); }
    fclose(o);
}

static void run_cycles(long long n) { long long end = cycles + n; while (cycles < end) step(); }

typedef struct { int a, b; uint8_t bits; } In;
static In ins[4096]; static int nin;
static uint8_t btn(const char *s)
{
    static const char *n[8] = { "A", "B", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT" }; int i;
    for (i = 0; i < 8; i++) if (!strcmp(s, n[i])) return 1 << i;
    return 0;
}

int main(int argc, char **argv)
{
    FILE *f; long sz; uint8_t *rom; int frames, fr, i; const char *dir = "."; char dumps[4096] = "";
    if (argc < 3) { fprintf(stderr, "usage: nesref rom frames [-i input] [-d list] [-o dir] [-c cov] [-s] [-t]\n"); return 2; }
    f = fopen(argv[1], "rb"); if (!f) return 2; fseek(f, 0, SEEK_END); sz = ftell(f); rewind(f);
    rom = malloc(sz); if (fread(rom, 1, sz, f) != (size_t)sz) return 2; fclose(f);
    prg_banks = rom[4]; chr_banks8 = rom[5]; prg = rom + 16; chr = prg + prg_banks * 0x4000;
    frames = atoi(argv[2]);
    for (i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "-i")) { FILE *g = fopen(argv[++i], "r"); char l[256]; while (g && fgets(l, sizeof l, g)) { char *p, *tok; In in; if (sscanf(l, "%d %d", &in.a, &in.b) != 2) continue; in.bits = 0; p = l; strtol(p, &p, 10); strtol(p, &p, 10); for (tok = strtok(p, " \t\r\n+"); tok; tok = strtok(0, " \t\r\n+")) in.bits |= btn(tok); ins[nin++] = in; } if (g) fclose(g); }
        else if (!strcmp(argv[i], "-d")) { snprintf(dumps, sizeof dumps, ",%s,", argv[++i]); }
        else if (!strcmp(argv[i], "-o")) dir = argv[++i];
        else if (!strcmp(argv[i], "-c")) { covf = fopen(argv[++i], "w"); cov = calloc(prg_banks * 0x4000 + 0x800, 1); }
        else if (!strcmp(argv[i], "-s")) stats = 1;
        else if (!strcmp(argv[i], "-p")) { proff = fopen(argv[++i], "w"); prof = calloc(16 * 0x10000, sizeof *prof); }
        else if (!strcmp(argv[i], "-t")) trace = 1;
    }
    PC = rd(0xfffc) | (rd(0xfffd) << 8);
    if (cov) cov_mark(PC, 4);
    for (fr = 0; fr < frames; fr++) {
        int y; char key[16];
        pad = 0; for (i = 0; i < nin; i++) if (fr >= ins[i].a && fr <= ins[i].b) pad |= ins[i].bits;
        frame_instr = frame_ppuw = frame_2002r = 0;
        if (trace) printf("---- frame %d\n", fr);
        /* pre-render line: copy vertical bits */
        ppustatus &= ~0x60;
        for (y = 0; y < 240; y++) {
            scanline = y;
            run_cycles(85);
            if (ppumask & 0x18) { if (y == 0) v = t; render_line(y);
                /* increment y */
                if ((v & 0x7000) != 0x7000) v += 0x1000; else { int cy; v &= ~0x7000; cy = (v & 0x03e0) >> 5;
                    if (cy == 29) { cy = 0; v ^= 0x0800; } else if (cy == 31) cy = 0; else cy++; v = (v & ~0x03e0) | (cy << 5); }
                v = (v & ~0x041f) | (t & 0x041f); }
            else memset(fb[y], pal[0], 256);
            run_cycles(29);
        }
        scanline = 240; run_cycles(114);
        ppustatus |= 0x80; if (ppuctrl & 0x80) nmi_pending = 2;
        scanline = 241; run_cycles(114 * 20);
        ppustatus &= 0x1f;
        scanline = -1; run_cycles(114);
        if (stats) printf("frame %d instr %ld ppuw %ld r2002 %ld ctrl %02x mask %02x mmc %02x/%02x/%02x/%02x\n", fr, frame_instr, frame_ppuw, frame_2002r, ppuctrl, ppumask, m_ctrl, m_chr0, m_chr1, m_prg);
        snprintf(key, sizeof key, ",%d,", fr);
        if (strstr(dumps, key)) dump(dir, fr);
    }
    if (covf) { for (i = 0; i < prg_banks * 0x4000; i++) if (cov[i] & 1) fprintf(covf, "E %d %04x\n", i / 0x4000, (i / 0x4000 == prg_banks - 1 ? 0xc000 : 0x8000) + (i & 0x3fff)); fclose(covf); }
    if (proff) { for (i = 0; i < 16 * 0x10000; i++) if (prof[i]) fprintf(proff, "%u %x %04x\n", prof[i], i >> 16, i & 0xffff); fclose(proff); }
    fprintf(stderr, "instructions %lld\n", instr_total);
    return 0;
}
