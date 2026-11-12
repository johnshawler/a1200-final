/* z80.h - a Z80 interpreter for the Neo Geo Pocket's sound processor, host only. It never runs on the Amiga.
 *
 * Include it in one C file after defining the four functions it calls:
 *     static uint8_t z80_mr(uint16_t a);            static void z80_mw(uint16_t a, uint8_t v);
 *     static uint8_t z80_in(uint16_t port);         static void z80_out(uint16_t port, uint8_t v);
 * All documented instructions, plus the undocumented halves of IX and IY. The undocumented flag bits 3 and 5
 * follow the result, which is right for most instructions and not for all. Timing is in T-states, close to
 * the real counts (prefixed instructions are rounded).
 */
#ifndef Z80_H
#define Z80_H
#include <stdint.h>

#define ZC 0x01
#define ZN 0x02
#define ZP 0x04
#define ZH 0x10
#define ZZ 0x40
#define ZS 0x80

typedef struct {
    uint8_t r[8];               /* B C D E H L - A  (index 6 is not used) */
    uint8_t f, r2[8], f2;       /* the alternate set */
    uint16_t ix, iy, sp, pc;
    uint8_t i, rr, iff1, iff2, im, halted, irq_line, nmi_pending;
} Z80;
static Z80 z80;
static uint8_t z80_szp[256];
static const uint8_t z80_cyc[256] = {
     4,10, 7, 6, 4, 4, 7, 4, 4,11, 7, 6, 4, 4, 7, 4,   8,10, 7, 6, 4, 4, 7, 4,12,11, 7, 6, 4, 4, 7, 4,
     7,10,16, 6, 4, 4, 7, 4, 7,11,16, 6, 4, 4, 7, 4,   7,10,13, 6,11,11,10, 4, 7,11,13, 6, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   7, 7, 7, 7, 7, 7, 4, 7, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,   4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
     5,10,10,10,10,11, 7,11, 5,10,10, 4,10,17, 7,11,   5,10,10,11,10,11, 7,11, 5, 4,10,11,10, 4, 7,11,
     5,10,10,19,10,11, 7,11, 5, 4,10, 4,10, 4, 7,11,   5,10,10, 4,10,11, 7,11, 5, 6,10, 4,10, 4, 7,11 };

#define Z_B z80.r[0]
#define Z_C z80.r[1]
#define Z_D z80.r[2]
#define Z_E z80.r[3]
#define Z_H z80.r[4]
#define Z_L z80.r[5]
#define Z_A z80.r[7]
#define Z_BC ((uint16_t)(Z_B << 8 | Z_C))
#define Z_DE ((uint16_t)(Z_D << 8 | Z_E))
#define Z_HL ((uint16_t)(Z_H << 8 | Z_L))

static void z80_reset(void)
{
    int i, j, p;
    memset(&z80, 0, sizeof z80);
    z80.sp = 0xffff; Z_A = 0xff; z80.f = 0xff;
    for (i = 0; i < 256; i++) {
        for (p = 0, j = 0; j < 8; j++) p ^= (i >> j) & 1;
        z80_szp[i] = (uint8_t)((i & 0xa8) | (i ? 0 : ZZ) | (p ? 0 : ZP));
    }
}
static uint8_t z80_fetch(void) { return z80_mr(z80.pc++); }
static uint16_t z80_fetch16(void) { uint8_t lo = z80_fetch(); return (uint16_t)(lo | z80_fetch() << 8); }
static uint16_t z80_rd16(uint16_t a) { return (uint16_t)(z80_mr(a) | z80_mr((uint16_t)(a + 1)) << 8); }
static void z80_wr16(uint16_t a, uint16_t v) { z80_mw(a, (uint8_t)v); z80_mw((uint16_t)(a + 1), (uint8_t)(v >> 8)); }
static void z80_push(uint16_t v) { z80.sp -= 2; z80_wr16(z80.sp, v); }
static uint16_t z80_pop(void) { uint16_t v = z80_rd16(z80.sp); z80.sp += 2; return v; }

static void z80_alu(int op, uint8_t v)                  /* ADD ADC SUB SBC AND XOR OR CP */
{
    uint8_t a = Z_A; unsigned r, c = z80.f & ZC;
    switch (op) {
    case 0: case 1:
        r = a + v + (op == 1 ? c : 0);
        z80.f = (uint8_t)((r & 0xa8) | ((r & 0xff) ? 0 : ZZ) | (r >> 8 & ZC) | ((a ^ v ^ r) & ZH) | (((~(a ^ v) & (a ^ r)) & 0x80) >> 5));
        Z_A = (uint8_t)r; break;
    case 2: case 3: case 7:
        r = a - v - (op == 3 ? c : 0);
        z80.f = (uint8_t)((r & 0xa8) | ((r & 0xff) ? 0 : ZZ) | ZN | (r >> 8 & ZC) | ((a ^ v ^ r) & ZH) | ((((a ^ v) & (a ^ r)) & 0x80) >> 5));
        if (op != 7) Z_A = (uint8_t)r; else z80.f = (uint8_t)((z80.f & ~0x28) | (v & 0x28));
        break;
    case 4: Z_A &= v; z80.f = z80_szp[Z_A] | ZH; break;
    case 5: Z_A ^= v; z80.f = z80_szp[Z_A]; break;
    default: Z_A |= v; z80.f = z80_szp[Z_A]; break;
    }
}
static uint8_t z80_inc(uint8_t v) { v++; z80.f = (uint8_t)((z80.f & ZC) | (v & 0xa8) | (v ? 0 : ZZ) | ((v & 15) ? 0 : ZH) | (v == 0x80 ? ZP : 0)); return v; }
static uint8_t z80_dec(uint8_t v) { v--; z80.f = (uint8_t)((z80.f & ZC) | ZN | (v & 0xa8) | (v ? 0 : ZZ) | ((v & 15) == 15 ? ZH : 0) | (v == 0x7f ? ZP : 0)); return v; }
static uint8_t z80_rot(int op, uint8_t v)               /* RLC RRC RL RR SLA SRA SLL SRL */
{
    uint8_t c = z80.f & ZC, out;
    switch (op) {
    case 0: out = v >> 7; v = (uint8_t)(v << 1 | out); break;
    case 1: out = v & 1; v = (uint8_t)(v >> 1 | out << 7); break;
    case 2: out = v >> 7; v = (uint8_t)(v << 1 | c); break;
    case 3: out = v & 1; v = (uint8_t)(v >> 1 | c << 7); break;
    case 4: out = v >> 7; v = (uint8_t)(v << 1); break;
    case 5: out = v & 1; v = (uint8_t)(v >> 1 | (v & 0x80)); break;
    case 6: out = v >> 7; v = (uint8_t)(v << 1 | 1); break;
    default: out = v & 1; v >>= 1; break;
    }
    z80.f = z80_szp[v] | out;
    return v;
}
static int z80_cc(int y)
{
    switch (y) {
    case 0: return !(z80.f & ZZ); case 1: return z80.f & ZZ; case 2: return !(z80.f & ZC); case 3: return z80.f & ZC;
    case 4: return !(z80.f & ZP); case 5: return z80.f & ZP; case 6: return !(z80.f & ZS); default: return z80.f & ZS;
    }
}
static uint16_t z80_add16(uint16_t a, uint16_t b)
{
    unsigned r = a + b;
    z80.f = (uint8_t)((z80.f & (ZS | ZZ | ZP)) | (r >> 16 & ZC) | ((a ^ b ^ r) >> 8 & ZH) | (r >> 8 & 0x28));
    return (uint16_t)r;
}

/* Execute one instruction (or take a pending interrupt). Returns the T-states used. */
static int z80_step(void)
{
    uint8_t op, x, y, z, p, q;
    uint16_t *xy = 0, hl, ea = 0;
    int cyc, d = 0, has_d = 0;

    if (z80.nmi_pending) { z80.nmi_pending = 0; z80.halted = 0; z80.iff1 = 0; z80_push(z80.pc); z80.pc = 0x66; return 11; }
    if (z80.irq_line && z80.iff1) { z80.halted = 0; z80.iff1 = z80.iff2 = 0; z80_push(z80.pc); z80.pc = 0x38; return 13; }
    if (z80.halted) return 4;
    op = z80_fetch(); cyc = 0;
    while (op == 0xdd || op == 0xfd) { xy = op == 0xdd ? &z80.ix : &z80.iy; op = z80_fetch(); cyc += 4; }
    cyc += z80_cyc[op];
    x = op >> 6; y = (op >> 3) & 7; z = op & 7; p = y >> 1; q = y & 1;
    hl = xy ? *xy : Z_HL;
#define SETHL(v) do { uint16_t t_ = (v); if (xy) *xy = t_; else { Z_H = (uint8_t)(t_ >> 8); Z_L = (uint8_t)t_; } } while (0)
#define MEMHL()  (xy ? (has_d ? ea : (has_d = 1, cyc += 8, d = (int8_t)z80_fetch(), ea = (uint16_t)(*xy + d))) : Z_HL)
#define GETR(n)  ((n) == 6 ? z80_mr(MEMHL()) : (xy && !has_d && (n) == 4) ? (uint8_t)(*xy >> 8) : (xy && !has_d && (n) == 5) ? (uint8_t)*xy : z80.r[n])
#define SETR(n, v) do { uint8_t v_ = (v); if ((n) == 6) z80_mw(MEMHL(), v_); else if (xy && !has_d && (n) == 4) *xy = (uint16_t)((*xy & 0xff) | v_ << 8); \
                        else if (xy && !has_d && (n) == 5) *xy = (uint16_t)((*xy & 0xff00) | v_); else z80.r[n] = v_; } while (0)
#define GETRP(n) ((n) == 0 ? Z_BC : (n) == 1 ? Z_DE : (n) == 2 ? hl : z80.sp)
#define SETRP(n, v) do { uint16_t w_ = (v); if ((n) == 0) { Z_B = (uint8_t)(w_ >> 8); Z_C = (uint8_t)w_; } else if ((n) == 1) { Z_D = (uint8_t)(w_ >> 8); Z_E = (uint8_t)w_; } \
                         else if ((n) == 2) SETHL(w_); else z80.sp = w_; } while (0)
    if (op == 0xcb) {                                   /* bit operations */
        uint8_t v; int r;
        if (xy) { d = (int8_t)z80_fetch(); ea = (uint16_t)(*xy + d); has_d = 1; cyc += 8; }
        op = z80_fetch(); x = op >> 6; y = (op >> 3) & 7; z = op & 7; r = xy ? 6 : z;
        v = r == 6 ? z80_mr(xy ? ea : Z_HL) : z80.r[r];
        cyc += r == 6 ? (x == 1 ? 8 : 11) : 4;
        if (x == 0) v = z80_rot(y, v);
        else if (x == 1) { z80.f = (uint8_t)((z80.f & ZC) | ZH | ((v & (1 << y)) ? 0 : ZZ | ZP) | (v & (1 << y) & 0x80)); return cyc; }
        else if (x == 2) v &= (uint8_t)~(1 << y);
        else v |= (uint8_t)(1 << y);
        if (r == 6) z80_mw(xy ? ea : Z_HL, v); else z80.r[r] = v;
        if (xy && z != 6) z80.r[z] = v;
        return cyc;
    }
    if (op == 0xed) {
        op = z80_fetch(); x = op >> 6; y = (op >> 3) & 7; z = op & 7; p = y >> 1; q = y & 1; cyc += 4;
        if (x == 1) {
            switch (z) {
            case 0: { uint8_t v = z80_in(Z_BC); if (y != 6) z80.r[y] = v; z80.f = (z80.f & ZC) | z80_szp[v]; cyc += 4; break; }
            case 1: z80_out(Z_BC, y == 6 ? 0 : z80.r[y]); cyc += 4; break;
            case 2: {
                uint16_t a = Z_HL, b = GETRP(p); unsigned c = z80.f & ZC, r;
                if (p == 2) b = Z_HL;
                if (q) { r = a + b + c; z80.f = (uint8_t)((r >> 16 & ZC) | ((a ^ b ^ r) >> 8 & ZH) | (((~(a ^ b) & (a ^ r)) & 0x8000) >> 13) | (r >> 8 & 0xa8) | ((r & 0xffff) ? 0 : ZZ)); }
                else { r = a - b - c; z80.f = (uint8_t)((r >> 16 & ZC) | ZN | ((a ^ b ^ r) >> 8 & ZH) | ((((a ^ b) & (a ^ r)) & 0x8000) >> 13) | (r >> 8 & 0xa8) | ((r & 0xffff) ? 0 : ZZ)); }
                Z_H = (uint8_t)(r >> 8); Z_L = (uint8_t)r; cyc += 7; break; }
            case 3: {
                uint16_t a = z80_fetch16();
                if (q) { uint16_t v = z80_rd16(a); if (p == 2) { Z_H = (uint8_t)(v >> 8); Z_L = (uint8_t)v; } else SETRP(p, v); }
                else z80_wr16(a, p == 2 ? Z_HL : GETRP(p));
                cyc += 12; break; }
            case 4: { uint8_t v = Z_A; Z_A = 0; z80_alu(2, v); break; }
            case 5: z80.pc = z80_pop(); z80.iff1 = z80.iff2; cyc += 6; break;
            case 6: z80.im = (y & 3) == 2 ? 1 : (y & 3) == 3 ? 2 : 0; break;
            default:
                switch (y) {
                case 0: z80.i = Z_A; cyc += 1; break;
                case 1: z80.rr = Z_A; cyc += 1; break;
                case 2: Z_A = z80.i; z80.f = (uint8_t)((z80.f & ZC) | (Z_A & 0xa8) | (Z_A ? 0 : ZZ) | (z80.iff2 ? ZP : 0)); cyc += 1; break;
                case 3: Z_A = z80.rr; z80.f = (uint8_t)((z80.f & ZC) | (Z_A & 0xa8) | (Z_A ? 0 : ZZ) | (z80.iff2 ? ZP : 0)); cyc += 1; break;
                case 4: { uint8_t m = z80_mr(Z_HL); z80_mw(Z_HL, (uint8_t)(Z_A << 4 | m >> 4)); Z_A = (uint8_t)((Z_A & 0xf0) | (m & 15)); z80.f = (z80.f & ZC) | z80_szp[Z_A]; cyc += 10; break; }
                case 5: { uint8_t m = z80_mr(Z_HL); z80_mw(Z_HL, (uint8_t)(m << 4 | (Z_A & 15))); Z_A = (uint8_t)((Z_A & 0xf0) | (m >> 4)); z80.f = (z80.f & ZC) | z80_szp[Z_A]; cyc += 10; break; }
                }
            }
        } else if (x == 2 && z <= 3 && y >= 4) {        /* block instructions */
            int dir = (y & 1) ? -1 : 1, rep = y >= 6;
            uint16_t h = Z_HL, bc = Z_BC, de = Z_DE;
            cyc += 12;
            if (z == 0) {                               /* LDI LDD LDIR LDDR */
                z80_mw(de, z80_mr(h)); h += dir; de += dir; bc--;
                z80.f = (uint8_t)((z80.f & (ZS | ZZ | ZC)) | (bc ? ZP : 0));
                Z_D = (uint8_t)(de >> 8); Z_E = (uint8_t)de;
                if (rep && bc) { z80.pc -= 2; cyc += 5; }
            } else if (z == 1) {                        /* CPI CPD CPIR CPDR */
                uint8_t m = z80_mr(h), r = (uint8_t)(Z_A - m);
                h += dir; bc--;
                z80.f = (uint8_t)((z80.f & ZC) | ZN | (r & ZS) | (r ? 0 : ZZ) | ((Z_A ^ m ^ r) & ZH) | (bc ? ZP : 0));
                if (rep && bc && r) { z80.pc -= 2; cyc += 5; }
            } else if (z == 2) {                        /* INI IND INIR INDR */
                z80_mw(h, z80_in(bc)); h += dir; bc = (uint16_t)((bc & 0xff) | ((bc >> 8) - 1) << 8);
                z80.f = (uint8_t)((z80.f & ZC) | ZN | ((bc >> 8) ? 0 : ZZ));
                if (rep && (bc >> 8)) { z80.pc -= 2; cyc += 5; }
            } else {                                    /* OUTI OUTD OTIR OTDR */
                bc = (uint16_t)((bc & 0xff) | ((bc >> 8) - 1) << 8); z80_out(bc, z80_mr(h)); h += dir;
                z80.f = (uint8_t)((z80.f & ZC) | ZN | ((bc >> 8) ? 0 : ZZ));
                if (rep && (bc >> 8)) { z80.pc -= 2; cyc += 5; }
            }
            Z_H = (uint8_t)(h >> 8); Z_L = (uint8_t)h; Z_B = (uint8_t)(bc >> 8); Z_C = (uint8_t)bc;
        }
        return cyc;
    }
    switch (x) {
    case 0:
        switch (z) {
        case 0:
            if (y == 0) break;
            if (y == 1) { uint8_t t = Z_A; Z_A = z80.r2[7]; z80.r2[7] = t; t = z80.f; z80.f = z80.f2; z80.f2 = t; break; }
            d = (int8_t)z80_fetch();
            if (y == 2) { if (--Z_B) { z80.pc += d; cyc += 5; } }
            else if (y == 3 || z80_cc(y - 4)) { z80.pc += d; if (y != 3) cyc += 5; }
            break;
        case 1: if (q) SETHL(z80_add16(hl, GETRP(p))); else SETRP(p, z80_fetch16()); break;
        case 2:
            if (p == 0) { if (q) Z_A = z80_mr(Z_BC); else z80_mw(Z_BC, Z_A); }
            else if (p == 1) { if (q) Z_A = z80_mr(Z_DE); else z80_mw(Z_DE, Z_A); }
            else if (p == 2) { uint16_t a = z80_fetch16(); if (q) SETHL(z80_rd16(a)); else z80_wr16(a, hl); }
            else { uint16_t a = z80_fetch16(); if (q) Z_A = z80_mr(a); else z80_mw(a, Z_A); }
            break;
        case 3: SETRP(p, (uint16_t)(GETRP(p) + (q ? -1 : 1))); break;
        case 4: { uint8_t v = GETR(y); v = z80_inc(v); SETR(y, v); break; }
        case 5: { uint8_t v = GETR(y); v = z80_dec(v); SETR(y, v); break; }
        case 6: if (y == 6) { uint16_t a = MEMHL(); z80_mw(a, z80_fetch()); if (xy) cyc -= 3; } else SETR(y, z80_fetch()); break;
        default:
            switch (y) {
            case 0: { uint8_t c = Z_A >> 7; Z_A = (uint8_t)(Z_A << 1 | c); z80.f = (uint8_t)((z80.f & (ZS | ZZ | ZP)) | c | (Z_A & 0x28)); break; }
            case 1: { uint8_t c = Z_A & 1; Z_A = (uint8_t)(Z_A >> 1 | c << 7); z80.f = (uint8_t)((z80.f & (ZS | ZZ | ZP)) | c | (Z_A & 0x28)); break; }
            case 2: { uint8_t c = Z_A >> 7; Z_A = (uint8_t)(Z_A << 1 | (z80.f & ZC)); z80.f = (uint8_t)((z80.f & (ZS | ZZ | ZP)) | c | (Z_A & 0x28)); break; }
            case 3: { uint8_t c = Z_A & 1; Z_A = (uint8_t)(Z_A >> 1 | (z80.f & ZC) << 7); z80.f = (uint8_t)((z80.f & (ZS | ZZ | ZP)) | c | (Z_A & 0x28)); break; }
            case 4: {                                   /* DAA */
                uint8_t a = Z_A, fix = 0, c = z80.f & ZC;
                if ((z80.f & ZH) || (a & 15) > 9) fix |= 6;
                if (c || a > 0x99) { fix |= 0x60; c = 1; }
                Z_A = (z80.f & ZN) ? (uint8_t)(a - fix) : (uint8_t)(a + fix);
                z80.f = (uint8_t)((z80.f & ZN) | c | z80_szp[Z_A] | ((a ^ Z_A) & ZH)); break; }
            case 5: Z_A = (uint8_t)~Z_A; z80.f |= ZH | ZN; break;
            case 6: z80.f = (uint8_t)((z80.f & (ZS | ZZ | ZP)) | ZC | (Z_A & 0x28)); break;
            default: z80.f = (uint8_t)(((z80.f & (ZS | ZZ | ZP | ZC)) | ((z80.f & ZC) ? ZH : 0) | (Z_A & 0x28)) ^ ZC); break;
            }
        }
        break;
    case 1:
        if (op == 0x76) { z80.halted = 1; break; }
        if (y == 6) { uint16_t a = MEMHL(); z80_mw(a, z80.r[z]); }                  /* LD (HL),r: r is never IXH/IXL */
        else if (z == 6) { uint16_t a = MEMHL(); z80.r[y] = z80_mr(a); }
        else { uint8_t v = GETR(z); SETR(y, v); }
        break;
    case 2: z80_alu(y, GETR(z)); break;
    default:
        switch (z) {
        case 0: if (z80_cc(y)) { z80.pc = z80_pop(); cyc += 6; } break;
        case 1:
            if (!q) { uint16_t v = z80_pop(); if (p == 3) { Z_A = (uint8_t)(v >> 8); z80.f = (uint8_t)v; } else SETRP(p, v); }
            else if (p == 0) z80.pc = z80_pop();
            else if (p == 1) { int i; for (i = 0; i < 6; i++) { uint8_t t = z80.r[i]; z80.r[i] = z80.r2[i]; z80.r2[i] = t; } }
            else if (p == 2) z80.pc = hl;
            else z80.sp = hl;
            break;
        case 2: { uint16_t a = z80_fetch16(); if (z80_cc(y)) z80.pc = a; break; }
        case 3:
            switch (y) {
            case 0: z80.pc = z80_fetch16(); break;
            case 2: z80_out((uint16_t)(Z_A << 8 | z80_fetch()), Z_A); break;
            case 3: Z_A = z80_in((uint16_t)(Z_A << 8 | z80_fetch())); break;
            case 4: { uint16_t t = z80_rd16(z80.sp); z80_wr16(z80.sp, hl); SETHL(t); break; }
            case 5: { uint8_t t = Z_D; Z_D = Z_H; Z_H = t; t = Z_E; Z_E = Z_L; Z_L = t; break; }
            case 6: z80.iff1 = z80.iff2 = 0; break;
            case 7: z80.iff1 = z80.iff2 = 1; break;
            }
            break;
        case 4: { uint16_t a = z80_fetch16(); if (z80_cc(y)) { z80_push(z80.pc); z80.pc = a; cyc += 7; } break; }
        case 5:
            if (!q) z80_push(p == 3 ? (uint16_t)(Z_A << 8 | z80.f) : GETRP(p));
            else if (p == 0) { uint16_t a = z80_fetch16(); z80_push(z80.pc); z80.pc = a; }
            break;
        case 6: z80_alu(y, z80_fetch()); break;
        default: z80_push(z80.pc); z80.pc = (uint16_t)(y * 8); break;
        }
    }
    return cyc;
#undef SETHL
#undef MEMHL
#undef GETR
#undef SETR
#undef GETRP
#undef SETRP
}
#endif
