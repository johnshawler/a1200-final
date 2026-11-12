#!/usr/bin/env python3
"""recomp.py - translate the NES game's 6502 code to C, once, on the host (the brief's route for 6502 games).

usage: recomp.py [--game name] rom.nes outdir flow1.txt [flow2.txt ...]   (game knowledge: tools/games/<name>.py)

flow files come from tools/nesref (option -c): executed instructions (E), indirect jump targets (I),
returns that did not go back to their caller (U), calls into the switchable bank from outside it (X).
They seed the entry points; everything reachable from them is decoded statically.

Output (all generated, never edited by hand):
  outdir/gen_code.h        declarations
  outdir/gen_bank<N>.c     one C function per entry point, per bank (bank 7 = the fixed bank at $C000)
  outdir/gen_dispatch.c    nes_dispatch(): entry address (in the mapped bank) -> function
  outdir/gen_waits.txt     the wait loops found, for the host interpreter that checks the translation

Model: the 6502 stack stays in NES RAM. JSR pushes its return address and calls the target's C function,
which returns the address its RTS popped (+1). If that is not the caller's own continuation the stack was
manipulated, and the caller returns it upward until a caller recognises it (or the top level dispatches it).
Registers and flags are C locals inside a function (gcc removes the flag work nobody reads) and go through
globals only across calls. A loop that only polls RAM or $2002 is a wait for the next frame: it calls
nes_wait(), which ends the frame and runs the NMI handler.
"""
import sys, os, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from op6502 import decode

# the per-game knowledge (tools/games/<name>.py): --game <name> as the first arguments
import importlib
args = sys.argv[1:]
gamename = 'llb'
if args[0] == '--game': gamename, args = args[1], args[2:]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'games'))
game = importlib.import_module(gamename)
NORETURN = game.NORETURN
sys.argv = [sys.argv[0]] + args

rom = open(sys.argv[1], 'rb').read()
nprg = rom[4]
prg = rom[16:16 + nprg * 16384]
FIXED = nprg - 1
outdir = sys.argv[2]

def rd(ctx, a):
    a &= 0xffff
    if a >= 0xc000: return prg[FIXED * 0x4000 + a - 0xc000]
    if a >= 0x8000: return prg[ctx * 0x4000 + a - 0x8000]
    return 0x02                 # not ROM: decodes as an unknown opcode

def ctx_of(ctx, a):
    """bank a code address belongs to, seen from code running with switchable bank ctx (7 = unknown)"""
    return FIXED if a >= 0xc000 else ctx

# ---------------------------------------------------------------- inputs
executed = set(); entries = set(); dyn = collections.defaultdict(set)
reset = prg[FIXED * 0x4000 + 0x3ffc] | prg[FIXED * 0x4000 + 0x3ffd] << 8
nmi = prg[FIXED * 0x4000 + 0x3ffa] | prg[FIXED * 0x4000 + 0x3ffb] << 8
irq = prg[FIXED * 0x4000 + 0x3ffe] | prg[FIXED * 0x4000 + 0x3fff] << 8
entries |= {(FIXED, reset), (FIXED, nmi), (FIXED, irq)}
for fn in sys.argv[3:]:
    for line in open(fn):
        p = line.split()
        if not p: continue
        if p[0] == 'E': executed.add((int(p[1]), int(p[2], 16)))
        elif p[0] in 'IUX':
            b, to = int(p[1]), int(p[2], 16)
            if to < 0x8000: continue
            entries.add((FIXED if to >= 0xc000 else b, to))
            dyn['all'].add((FIXED if to >= 0xc000 else b, to))
            if p[0] == 'I': dyn['I'].add((b, to))

# ---------------------------------------------------------------- decoding
insn_cache = {}
def insn(ctx, a):
    key = (ctx_of(ctx, a), a)
    if key not in insn_cache:
        insn_cache[key] = decode(lambda x: rd(key[0], x), a)
    return insn_cache[key]

def static_branch(ctx, a, prev):
    """None, or True/False when the branch outcome is fixed by the instruction just before it"""
    m = insn(ctx, a)[0]
    if prev is None: return None
    pm, pmode, parg, _ = prev
    if pmode == 'imm' and pm in ('LDA', 'LDX', 'LDY'):
        if m == 'BEQ': return parg == 0
        if m == 'BNE': return parg != 0
        if m == 'BMI': return parg >= 0x80
        if m == 'BPL': return parg < 0x80
    if pm == 'CLC' and m in ('BCC', 'BCS'): return m == 'BCC'
    if pm == 'SEC' and m in ('BCC', 'BCS'): return m == 'BCS'
    if pm == 'CLV' and m in ('BVC', 'BVS'): return m == 'BVC'
    return None

def successors(ctx, a, prev):
    """(fallthrough?, list of local jump targets, call target or None)"""
    d = insn(ctx, a)
    if not d: return False, [], None
    m, mode, arg, n = d
    if m in ('RTS', 'RTI', 'BRK'): return False, [], None
    if m == 'JMP': return False, ([arg] if mode == 'abs' else []), None
    if m == 'JSR': return (arg not in NORETURN), [], arg
    if mode == 'rel':
        sb = static_branch(ctx, a, prev)
        if sb is True: return False, [arg], None
        if sb is False: return True, [], None
        return True, [arg], None
    return True, [], None

def body(ctx, entry, stop_at_entries):
    """addresses of the instructions of the function starting at entry; JMPs to other entries are tail calls"""
    seen = {}; work = [(entry, None)]
    while work:
        a, prev = work.pop()
        if a in seen: continue
        if a < 0x8000 or (ctx == FIXED and a < 0xc000): continue     # RAM, or the unknown bank: dispatched at run time
        seen[a] = True
        d = insn(ctx, a)
        if not d: continue
        ft, targets, call = successors(ctx, a, prev)
        if ft: work.append(((a + d[3]) & 0xffff, d))
        for t in targets:
            if d[0] == 'JMP' and stop_at_entries and (ctx_of(ctx, t), t) in entries and t != entry: continue
            work.append((t, d))
    return sorted(seen)

# fixpoint over JSR targets
while True:
    new = set()
    for (ctx, e) in list(entries):
        for a in body(ctx, e, False):
            d = insn(ctx, a)
            if d and d[0] == 'JSR':
                t = d[2]
                if t >= 0xc000: new.add((FIXED, t))
                elif t >= 0x8000 and (ctx != FIXED and a < 0xc000): new.add((ctx, t))
                elif t >= 0x8000 and ctx != FIXED: new.add((ctx, t))
            if d and d[0] == 'JSR' and d[2] in NORETURN:      # the words after it are jump targets
                tb = (a + 3) & 0xffff
                for k in range(64):
                    w = rd(ctx, tb + 2 * k) | rd(ctx, tb + 2 * k + 1) << 8
                    if w < 0x8000 or (ctx == FIXED and w < 0xc000 and (ctx, w) not in entries): break
                    if (ctx_of(ctx, w), w) in executed or (FIXED if w >= 0xc000 else ctx, w) in dyn['I']:
                        new.add((ctx_of(ctx, w), w)); dyn['all'].add((ctx_of(ctx, w), w))
    new -= entries
    if not new: break
    entries |= new

# ---------------------------------------------------------------- wait loops
# register uses/defs of the instructions allowed in a wait loop (no memory writes, no calls, no stack)
_UD = {'LDA': ('', 'a'), 'LDX': ('', 'x'), 'LDY': ('', 'y'), 'BIT': ('', ''), 'AND': ('a', 'a'), 'ORA': ('a', 'a'),
       'EOR': ('a', 'a'), 'CMP': ('a', ''), 'CPX': ('x', ''), 'CPY': ('y', ''), 'INX': ('x', 'x'), 'DEX': ('x', 'x'),
       'INY': ('y', 'y'), 'DEY': ('y', 'y'), 'TAX': ('a', 'x'), 'TAY': ('a', 'y'), 'TXA': ('x', 'a'), 'TYA': ('y', 'a'),
       'ASL': ('a', 'a'), 'LSR': ('a', 'a'), 'NOP': ('', '')}

def wait_kind(ctx, start, br):
    """the loop start..br (br = the branch back) can only end when an interrupt changes memory:
    it writes nothing, reads fixed addresses only, and sets every register before using it.
    RAM poll -> 'RAM', $2002 with a bit 7 test -> 'VBL', with a bit 6 test -> 'S0'"""
    a, reads, defined, bits = start, [], set(), set()
    while a <= br:
        d = insn(ctx, a)
        if not d: return None
        m, mode, arg, n = d
        if mode == 'rel': bits.add(m)
        elif m in _UD and mode in ('zp', 'abs', 'imm', 'imp', 'acc'):
            use, dfn = _UD[m]
            if use and use not in defined: return None
            if mode in ('zp', 'abs'): reads.append(arg)
            if dfn: defined.add(dfn)
        else: return None
        a += n
    if not reads or (a - start) > 16: return None
    if 0x2002 in reads:
        return 'S0' if bits & {'BVC', 'BVS'} else 'VBL'
    if all(r < 0x2000 for r in reads): return 'RAM'
    return None

# ---------------------------------------------------------------- C generation
def fname(ctx, a): return 'F%d_%04x' % (ctx_of(ctx, a), a)

def call_expr(ctx, at, t):
    """C expression that runs the subroutine at t, called from code at address 'at' in a function of bank ctx"""
    if t >= 0xc000: return fname(FIXED, t) + '()'
    if t < 0x8000: return 'nes_dispatch(0x%04x)' % t
    if ctx == FIXED: return 'nes_dispatch(0x%04x)' % t
    if (ctx, t) in entries:
        if at < 0xc000: return fname(ctx, t) + '()'
        return '(nes_bank == %d ? %s() : nes_dispatch(0x%04x))' % (ctx, fname(ctx, t), t)
    return 'nes_dispatch(0x%04x)' % t

def operand(ctx, m, mode, arg, s0):
    """returns (setup statement, read expression, write format with {v}) for a memory operand"""
    if mode in ('zp',):
        return '', 'M[0x%02x]' % arg, 'M[0x%02x] = {v};' % arg
    if mode in ('zpx', 'zpy'):
        r = mode[-1]
        e = 'M[(uint8_t)(0x%02x + %s)]' % (arg, r)
        return '', e, e + ' = {v};'
    if mode == 'abs':
        if arg < 0x2000: return '', 'M[0x%03x]' % (arg & 0x7ff), 'M[0x%03x] = {v};' % (arg & 0x7ff)
        if arg >= 0xc000: rde = 'P[0x%05x]' % (FIXED * 0x4000 + arg - 0xc000)
        elif arg >= 0x8000: rde = 'nes_prg_lo[0x%04x]' % (arg - 0x8000)
        elif arg == 0x2002 and s0: rde = 'nes_read_s0()'
        else: rde = 'nes_io_read(0x%04x)' % arg
        return '', rde, 'nes_write_slow(0x%04x, {v});' % arg
    if mode in ('abx', 'aby'):
        r = mode[-1]
        if arg + 255 < 0x800: e = 'M[0x%03x + %s]' % (arg, r); return '', e, e + ' = {v};'
        if arg + 255 < 0x2000: e = 'M[(0x%04x + %s) & 0x7ff]' % (arg, r); return '', e, e + ' = {v};'
        if arg >= 0xc000 and arg + 255 <= 0xffff:
            return 'ea = 0x%04x + %s;' % (arg, r), 'P[0x%05x + %s]' % (FIXED * 0x4000 + arg - 0xc000, r), 'nes_write_slow(ea, {v});'
        if arg >= 0x8000 and arg + 255 < 0xc000:
            return 'ea = 0x%04x + %s;' % (arg, r), 'nes_prg_lo[0x%04x + %s]' % (arg - 0x8000, r), 'nes_write_slow(ea, {v});'
        return 'ea = (uint16_t)(0x%04x + %s);' % (arg, r), 'nes_rd(ea)', 'nes_wr(ea, {v});'
    if mode == 'izx':
        return 'm = (uint8_t)(0x%02x + x); ea = (uint16_t)(M[m] | M[(uint8_t)(m + 1)] << 8);' % arg, 'nes_rd(ea)', 'nes_wr(ea, {v});'
    if mode == 'izy':
        return 'ea = (uint16_t)((M[0x%02x] | M[0x%02x] << 8) + y);' % (arg, (arg + 1) & 0xff), 'nes_rd(ea)', 'nes_wr(ea, {v});'
    if mode == 'imm':
        return '', '0x%02x' % arg, None
    raise ValueError(mode)

BR = {'BPL': '!(nv & 0x80)', 'BMI': '(nv & 0x80)', 'BVC': '!vf', 'BVS': 'vf', 'BCC': '!c', 'BCS': 'c', 'BNE': 'zv', 'BEQ': '!zv'}
PUSH = 'M[0x100 + s] = {v}; s--;'
PULL = 's++; {d} = M[0x100 + s];'
PACK = '(uint8_t)((nv & 0x80) | (vf ? 0x40 : 0) | 0x20 | (cpu_d ? 8 : 0) | (cpu_i ? 4 : 0) | (zv ? 0 : 2) | c)'

waits_found = []
s0_found = set()

def gen_function(ctx, entry):
    addrs = body(ctx, entry, True)
    aset = set(addrs)
    out, labels = [], set()
    s0_loops = set()
    # find wait loops: backward branches whose loop only reads
    waits = {}
    for a in addrs:
        d = insn(ctx, a)
        if d and d[1] == 'rel' and d[2] <= a and d[2] in aset:
            k = wait_kind(ctx, d[2], a)
            if k == 'S0':
                for x in addrs:
                    if d[2] <= x <= a: s0_loops.add(x); s0_found.add((ctx_of(ctx, x), x))
            elif k:
                waits[a] = (k, d[2]); waits_found.append((ctx_of(ctx, a), a, d[2], k))
    # also the exit-branch form of the sprite 0 wait: BIT $2002 / BMI out / BVC back
    body_lines = []
    prev = None
    for idx, a in enumerate(addrs):
        d = insn(ctx, a)
        nxt = addrs[idx + 1] if idx + 1 < len(addrs) else None
        L = []
        if not d:
            L.append('SAVE; nes_fatal(0x%04x, 1); return 0;' % a)
            body_lines.append((a, L)); prev = None; continue
        m, mode, arg, n = d
        fall = (a + n) & 0xffff
        s0 = a in s0_loops
        if mode in ('imp', 'acc', 'rel') or (m in ('JMP', 'JSR')): setup, rde, wrf = '', None, None
        else: setup, rde, wrf = operand(ctx, m, mode, arg, s0)
        if setup: L.append(setup)
        nf = 'zv = nv = %s;'
        if m == 'LDA': L.append('a = %s; ' % rde + nf % 'a')
        elif m == 'LDX': L.append('x = %s; ' % rde + nf % 'x')
        elif m == 'LDY': L.append('y = %s; ' % rde + nf % 'y')
        elif m == 'STA': L.append(wrf.format(v='a'))
        elif m == 'STX': L.append(wrf.format(v='x'))
        elif m == 'STY': L.append(wrf.format(v='y'))
        elif m == 'TAX': L.append('x = a; ' + nf % 'x')
        elif m == 'TAY': L.append('y = a; ' + nf % 'y')
        elif m == 'TXA': L.append('a = x; ' + nf % 'a')
        elif m == 'TYA': L.append('a = y; ' + nf % 'a')
        elif m == 'TSX': L.append('x = s; ' + nf % 'x')
        elif m == 'TXS': L.append('s = x;')
        elif m == 'ORA': L.append('a |= %s; ' % rde + nf % 'a')
        elif m == 'AND': L.append('a &= %s; ' % rde + nf % 'a')
        elif m == 'EOR': L.append('a ^= %s; ' % rde + nf % 'a')
        elif m == 'ADC': L.append('m = %s; t = a + m + c; vf = (uint8_t)(~(a ^ m) & (a ^ t) & 0x80); c = (uint8_t)(t >> 8); a = (uint8_t)t; ' % rde + nf % 'a')
        elif m == 'SBC': L.append('m = (uint8_t)~%s; t = a + m + c; vf = (uint8_t)(~(a ^ m) & (a ^ t) & 0x80); c = (uint8_t)(t >> 8); a = (uint8_t)t; ' % rde + nf % 'a')
        elif m in ('CMP', 'CPX', 'CPY'):
            r = {'CMP': 'a', 'CPX': 'x', 'CPY': 'y'}[m]
            L.append('m = %s; c = %s >= m; zv = nv = (uint8_t)(%s - m);' % (rde, r, r))
        elif m == 'BIT': L.append('m = %s; nv = m; vf = m & 0x40; zv = a & m;' % rde)
        elif m in ('ASL', 'LSR', 'ROL', 'ROR'):
            if mode == 'acc': src, dst = 'a', 'a = m;'
            else: src, dst = rde, wrf.format(v='m')
            op = {'ASL': 'm = %s; c = m >> 7; m = (uint8_t)(m << 1);',
                  'LSR': 'm = %s; c = m & 1; m >>= 1;',
                  'ROL': 'm = %s; t = c; c = m >> 7; m = (uint8_t)((m << 1) | t);',
                  'ROR': 'm = %s; t = c; c = m & 1; m = (uint8_t)((m >> 1) | (t << 7));'}[m] % src
            L.append(op + ' ' + dst + ' zv = nv = m;')
        elif m in ('INC', 'DEC'):
            L.append('m = (uint8_t)(%s %s 1); %s zv = nv = m;' % (rde, '+' if m == 'INC' else '-', wrf.format(v='m')))
        elif m == 'INX': L.append('x++; ' + nf % 'x')
        elif m == 'DEX': L.append('x--; ' + nf % 'x')
        elif m == 'INY': L.append('y++; ' + nf % 'y')
        elif m == 'DEY': L.append('y--; ' + nf % 'y')
        elif m == 'CLC': L.append('c = 0;')
        elif m == 'SEC': L.append('c = 1;')
        elif m == 'CLV': L.append('vf = 0;')
        elif m == 'CLI': L.append('cpu_i = 0;')
        elif m == 'SEI': L.append('cpu_i = 1;')
        elif m == 'CLD': L.append('cpu_d = 0;')
        elif m == 'SED': L.append('cpu_d = 1;')
        elif m == 'NOP': pass
        elif m == 'PHA': L.append(PUSH.format(v='a'))
        elif m == 'PHP': L.append(PUSH.format(v='(uint8_t)(%s | 0x10)' % PACK))
        elif m == 'PLA': L.append(PULL.format(d='a') + ' ' + nf % 'a')
        elif m == 'PLP': L.append(PULL.format(d='m') + ' nv = m; vf = m & 0x40; cpu_d = (m >> 3) & 1; cpu_i = (m >> 2) & 1; zv = !(m & 2); c = m & 1;')
        elif mode == 'rel':
            sb = static_branch(ctx, a, prev)
            cond = BR[m]
            if a in waits:
                kind, lp = waits[a]
                labels.add(lp)
                L.append('if (%s) { SAVE; nes_wait(%s, 0x%04x); LOAD; goto L_%04x; }' % (cond, 'WAIT_' + kind, lp, lp))
            elif sb is True:
                labels.add(arg); L.append('goto L_%04x;' % arg)
            elif sb is False: pass
            else:
                if arg in aset: labels.add(arg); L.append('if (%s) goto L_%04x;' % (cond, arg))
                else: L.append('if (%s) { SAVE; return %s; }' % (cond, call_expr(ctx, a, arg)))
        elif m == 'JMP':
            if mode == 'abs':
                if arg in aset: labels.add(arg); L.append('goto L_%04x;' % arg)
                else: L.append('SAVE; return %s;' % call_expr(ctx, a, arg))
            else:
                hi = (arg & 0xff00) | ((arg + 1) & 0xff)
                if arg < 0x2000: L.append('ea = (uint16_t)(M[0x%03x] | M[0x%03x] << 8);' % (arg & 0x7ff, hi & 0x7ff))
                else: L.append('ea = (uint16_t)(nes_rd(0x%04x) | nes_rd(0x%04x) << 8);' % (arg, hi))
                L.append('SAVE; return nes_dispatch(ea);')
        elif m == 'JSR':
            ret = (a + 2) & 0xffff
            L.append(PUSH.format(v='0x%02x' % (ret >> 8)) + ' ' + PUSH.format(v='0x%02x' % (ret & 0xff)))
            if arg in NORETURN:
                L.append('SAVE; return %s;' % call_expr(ctx, a, arg))
            else:
                L.append('SAVE; r = %s; if (r != 0x%04x) return r; LOAD;' % (call_expr(ctx, a, arg), (ret + 1) & 0xffff))
        elif m == 'RTS':
            L.append('s++; t = M[0x100 + s]; s++; t |= M[0x100 + s] << 8; SAVE; return (uint16_t)(t + 1);')
        elif m == 'RTI':
            L.append(PULL.format(d='m') + ' nv = m; vf = m & 0x40; cpu_d = (m >> 3) & 1; cpu_i = (m >> 2) & 1; zv = !(m & 2); c = m & 1;')
            L.append('s++; t = M[0x100 + s]; s++; t |= M[0x100 + s] << 8; SAVE; return (uint16_t)t;')
        elif m == 'BRK':
            L.append('SAVE; nes_fatal(0x%04x, 2); return 0;' % a)
        else:
            raise ValueError(m)
        # fallthrough into an instruction that is not emitted next
        ft, _, _ = successors(ctx, a, prev)
        if m in ('JSR',) and arg in NORETURN: ft = False
        if mode == 'rel' and static_branch(ctx, a, prev) is True: ft = False
        if ft and nxt != fall:
            if fall in aset: labels.add(fall); L.append('goto L_%04x;' % fall)
            else: L.append('SAVE; return %s;' % call_expr(ctx, a, fall))
        body_lines.append((a, L))
        prev = d
    # Instructions reached only by a goto: their fall-through state above was computed with the
    # sequential predecessor, which is what a branch target sees too only if the flags do not matter.
    out.append('uint16_t %s(void)\n{\n    REGS; LOAD;' % fname(ctx, entry))
    if addrs and addrs[0] != entry: labels.add(entry); out.insert(len(out), '    goto L_%04x;' % entry)
    for a, L in body_lines:
        lab = 'L_%04x: ' % a if a in labels else ''
        out.append('%s    %s' % (lab, ' '.join(L)) if lab else '    ' + ' '.join(L))
    out.append('}\n')
    return '\n'.join(out)

HDR = '''/* generated by tools/recomp.py from the game's ROM: do not edit */
#include "nes.h"
#define M nes_ram
#define P nes_prg_data
extern const uint8_t nes_prg_data[];
#define REGS uint8_t a, x, y, s, c, zv, nv, vf, m; unsigned t; uint16_t ea, r; (void)m; (void)t; (void)ea; (void)r
#define LOAD a = cpu_a; x = cpu_x; y = cpu_y; s = cpu_s; c = cpu_c; zv = cpu_z; nv = cpu_n; vf = cpu_v
#define SAVE cpu_a = a; cpu_x = x; cpu_y = y; cpu_s = s; cpu_c = c; cpu_z = zv; cpu_n = nv; cpu_v = vf
'''

os.makedirs(outdir, exist_ok=True)
byctx = collections.defaultdict(list)
for (ctx, e) in sorted(entries): byctx[ctx].append(e)
decls = []
for ctx in range(nprg):                 # every bank gets a file, even without code yet (port.conf lists them all)
    with open(os.path.join(outdir, 'gen_bank%d.c' % ctx), 'w') as f:
        f.write(HDR + '#include "gen_code.h"\n\n')
        for e in byctx.get(ctx, []):
            f.write(gen_function(ctx, e))
        if not byctx.get(ctx): f.write('typedef int gen_bank%d_is_empty;\n' % ctx)
    decls += ['uint16_t %s(void);' % fname(ctx, e) for e in byctx.get(ctx, [])]
with open(os.path.join(outdir, 'gen_code.h'), 'w') as f:
    f.write('/* generated by tools/recomp.py: do not edit */\n' + '\n'.join(decls) + '\n#define NES_NMI_FN %s\n#define NES_RESET_FN %s\n' % (fname(FIXED, nmi), fname(FIXED, reset)))
with open(os.path.join(outdir, 'gen_dispatch.c'), 'w') as f:
    f.write(HDR + '#include "gen_code.h"\n\nuint16_t nes_dispatch(uint16_t pc)\n{\n    if (pc >= 0xc000) switch (pc) {\n')
    for e in byctx[FIXED]: f.write('    case 0x%04x: return %s();\n' % (e, fname(FIXED, e)))
    f.write('    } else if (pc >= 0x8000) switch (nes_bank) {\n')
    for ctx in sorted(byctx):
        if ctx == FIXED: continue
        f.write('    case %d: switch (pc) {\n' % ctx)
        for e in byctx[ctx]: f.write('        case 0x%04x: return %s();\n' % (e, fname(ctx, e)))
        f.write('        } break;\n')
    f.write('    }\n    nes_fatal(pc, 3);\n    return 0;\n}\n\nuint16_t nes_reset_entry(void) { return %s(); }\n' % fname(FIXED, reset))
with open(os.path.join(outdir, 'gen_waits.txt'), 'w') as f:
    for w in sorted(set(waits_found)): f.write('%d %04x %04x %s\n' % w)
    for b, x in sorted(s0_found): f.write('%d %04x 0000 S0\n' % (b, x))

# report: decoded code that the traces never executed, and executed code not decoded
decoded = set()
for (ctx, e) in entries:
    for a in body(ctx, e, True): decoded.add((ctx_of(ctx, a), a))
print('entries %d, decoded instructions %d (executed in traces %d, executed but not decoded %d), wait loops %d'
      % (len(entries), len(decoded), len(executed), len(executed - decoded), len(set(waits_found))))
miss = sorted(executed - decoded)
if miss: print('not decoded, first:', ' '.join('%d:%04x' % x for x in miss[:20]))

# ---------------------------------------------------------------- the 68k assembly back end
import back68k
dynamic_entries = dyn['all'] | {(FIXED, reset), (FIXED, nmi), (FIXED, irq)}
back68k.generate(globals())
