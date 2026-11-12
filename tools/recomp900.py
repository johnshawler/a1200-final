#!/usr/bin/env python3
"""recomp900.py - translate a Neo Geo Pocket game's TLCS-900/H code to C, once, on the host.

usage: recomp900.py [--game name] rom.ngc outdir flow1.txt [flow2.txt ...]   (game knowledge: tools/games/<name>.py)

Flow files come from tools/ngpref (option -c): executed instructions (E), targets of jumps and calls through a
register or memory with the address of the jump or call (I, C), interrupt handlers (V). They seed the entry
points; everything reachable from them is decoded statically.

Output (all generated, never edited by hand):
  outdir/gen_code.h        declarations
  outdir/gen_NN.c          the functions, one C function per entry point, in files of about 2,500 lines
  outdir/gen_dispatch.c    ngp_dispatch(): entry address -> function
  outdir/gen_waits.h       the loops where the game waits, for the interpreter that checks the translation
  outdir/gen_game.h        what tools/games/<name>.py says the run time must know

Model. The TLCS-900/H stack stays in the game's RAM. CALL pushes its return address and calls the target's C
function, which returns the address where execution goes on: what its RET popped. If that is not the caller's
own continuation the stack was manipulated, and the caller returns it upward until a caller recognises it (or
the top level dispatches it). A jump to another function is a C tail call; a jump through a register becomes a
switch over the targets the traces saw. A loop that only re-reads memory is a wait: it ends the frame
(ngp_wait) or, when it counts down meanwhile, gives the sound processor time (ngp_poll).

Instructions. The registers and flags live in the one CPU state of src/ngpc/cpu900.h, and the generated code
is built from that file's primitives (cpu_alu, cpu_rd, cpu_setr, ...), so a flag is computed in one place
only. Instruction forms this translator has no native code for yet are executed by cpu_step() on the spot
(STEP): always correct, slower. The report at the end says how much of the traced code that is.
"""
import sys, os, collections, importlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tlcs900 as T

args = sys.argv[1:]
gamename = None
if args and args[0] == '--game': gamename, args = args[1], args[2:]
if len(args) < 2: print(__doc__); sys.exit(2)
class _NoGame: pass
game = _NoGame()
if gamename:
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'games'))
    game = importlib.import_module(gamename)
EXTRA_ENTRIES = set(getattr(game, 'EXTRA_ENTRIES', ())) # functions no trace reached
NOT_WAITS = set(getattr(game, 'NOT_WAITS', ()))         # branches that look like a wait loop and are not
NATIVE = getattr(game, 'NATIVE', True)                  # False: every instruction through cpu_step() (to find a translator bug)

rom = open(args[0], 'rb').read()
outdir = args[1]

def in_rom(a): return 0x200000 <= a < 0x200000 + min(len(rom), 0x200000) or 0x800000 <= a < 0x600000 + len(rom)
def rd(a):
    a &= 0xffffff
    if 0x200000 <= a < 0x400000: return rom[a - 0x200000] if a - 0x200000 < len(rom) else 0xff
    if 0x800000 <= a < 0xa00000: return rom[a - 0x600000] if a - 0x600000 < len(rom) else 0xff
    return 0xff

# ---------------------------------------------------------------------------------------------------- inputs
executed = set(); ind_jump = collections.defaultdict(set); ind_call = set(); vectors = set()
start = (rom[0x1c] | rom[0x1d] << 8 | rom[0x1e] << 16) & 0xffffff
for fn in args[2:]:
    for line in open(fn):
        p = line.split()
        if not p: continue
        if p[0] == 'E': executed.add(int(p[1], 16))
        elif p[0] == 'I': ind_jump[int(p[2], 16)].add(int(p[1], 16))
        elif p[0] == 'C': ind_call.add(int(p[1], 16))
        elif p[0] == 'V': vectors.add(int(p[2], 16))
ram_code = sorted(a for a in executed if not in_rom(a) and a < 0xff0000)

insn_cache = {}
def insn(a):
    if a not in insn_cache: insn_cache[a] = T.decode(rd, a) if in_rom(a) else None
    return insn_cache[a]

def successors(a):
    """(falls through?, static targets inside the function, static call target or None)"""
    d = insn(a)
    if not d: return False, [], None
    kind, cc, target = T.flow(d)
    if kind == 'ret': return cc is not None, [], None
    if kind == 'jump': return False, ([target] if target is not None else sorted(ind_jump.get(a, ()))), None
    if kind == 'branch': return True, ([target] if target is not None else sorted(ind_jump.get(a, ()))), None
    if kind == 'call': return True, [], target
    if kind == 'swi': return d.ops[0][1] == 1, [], None
    return True, [], None

# every instruction reachable from the entry points, and every call target among them
entries = {start} | {v for v in vectors if in_rom(v)} | {c for c in ind_call if in_rom(c)} | {e for e in EXTRA_ENTRIES if in_rom(e)}
reach = set(); work = list(entries)
while work:
    a = work.pop()
    if a in reach or not in_rom(a): continue
    reach.add(a)
    d = insn(a)
    if not d: continue
    ft, targets, call = successors(a)
    if ft: work.append((a + d.length) & 0xffffff)
    work += targets
    if call is not None and in_rom(call) and call not in entries: entries.add(call); work.append(call)

def body(entry):
    """addresses of the instructions of the function at entry: a jump to another function's entry is a tail call"""
    seen = set(); work = [entry]
    while work:
        a = work.pop()
        if a in seen or not in_rom(a) or (a in entries and a != entry): continue
        seen.add(a)
        d = insn(a)
        if not d: continue
        ft, targets, _ = successors(a)
        if ft: work.append((a + d.length) & 0xffffff)
        work += targets
    return sorted(seen)

# ---------------------------------------------------------------------------------------------------- wait loops
READS_ONLY = ('cp', 'bit')
def wait_kind(aset, lo, hi):
    """The loop lo..hi (hi = a conditional branch back to lo). 1 = it only re-reads memory: it waits for an
    interrupt handler, the frame ends there. 2 = it re-reads memory and counts down a register meanwhile: it
    waits for the sound processor (or gives up). None = an ordinary loop."""
    if hi in NOT_WAITS or hi - lo > 40: return None
    a, reads, written, addr_regs, pure = lo, 0, set(), set(), True
    while a <= hi:
        d = insn(a)
        if not d or a not in aset: return None
        kind = T.flow(d)[0]
        if kind in ('call', 'ret', 'swi', 'halt') or (kind in ('jump', 'branch') and T.flow(d)[2] is None): return None
        if kind is None:
            if d.mnem in T.BLOCK or d.mnem in T.COMPARE or d.mnem in ('push', 'pop', 'ex', 'ldc', 'link', 'unlk', 'mula', 'rld', 'rrd'): return None
            ops = d.ops
            for i, o in enumerate(ops):
                if o[0] == 'm':
                    if o[1] in ('inc', 'dec'): return None
                    if o[1] in ('ind', 'disp', 'idx'): addr_regs.add(o[2] & 0xfc)
                    if o[1] == 'idx': addr_regs.add(o[3] & 0xfc)
                    is_dest = i == 0 and d.mnem not in READS_ONLY
                    if is_dest or d.mnem in ('tset', 'set', 'res', 'chg', 'stcf'): return None      # a memory write
                    reads += 1
            if ops and ops[0][0] == 'r' and d.mnem not in READS_ONLY:
                written.add(ops[0][2] & 0xfc)
                if not (d.mnem == 'ld' and len(ops) == 2 and ops[1][0] in ('m', 'i')): pure = False     # not a plain reload
        elif d.mnem == 'djnz': written.add(d.ops[0][2] & 0xfc); pure = False
        a += d.length
    if not reads or addr_regs & written: return None
    return 1 if pure else 2

# ---------------------------------------------------------------------------------------------------- C generation
def fname(a): return 'F_%06x' % a
stats = collections.Counter()

ALU = {'add': 0, 'adc': 1, 'sub': 2, 'sbc': 3, 'and': 4, 'xor': 5, 'or': 6, 'cp': 7}
SHIFT = {'rlc': 0, 'rrc': 1, 'rl': 2, 'rr': 3, 'sla': 4, 'sra': 5, 'sll': 6, 'srl': 7}

def ea_code(o):
    """(statements that must run first, C expression of the address) of a memory operand. The interpreter moves
    an address register of (-r) and (r+) before the operation, so the statements come first here too."""
    m = o[1]
    if m in ('abs', 'pcrel'): return '', '0x%06xu' % o[2]
    if m == 'ind': return '', 'cpu_getr(4, 0x%02x)' % o[2]
    if m == 'disp': return '', '(cpu_getr(4, 0x%02x) + (uint32_t)(%d))' % (o[2], o[3])
    if m == 'idx': return '', '(cpu_getr(4, 0x%02x) + (uint32_t)(int%d_t)cpu_getr(%d, 0x%02x))' % (o[2], o[4] * 8, o[4], o[3])
    if m == 'dec': return 'ea = cpu_getr(4, 0x%02x) - %d; cpu_setr(4, 0x%02x, ea);' % (o[2], o[3], o[2]), 'ea'
    if m == 'inc': return 'ea = cpu_getr(4, 0x%02x); cpu_setr(4, 0x%02x, ea + %d);' % (o[2], o[2], o[3]), 'ea'
    raise ValueError(m)

def native(d):
    """C statements for a non-flow instruction, or None: then it is run by the interpreter's step on the spot.
    Every form here mirrors the case of the same instruction in src/ngpc/cpu900.h."""
    if not NATIVE: return None
    m, ops, s = d.mnem, d.ops, d.size
    kinds = tuple(o[0] for o in ops)
    if any(k in ('sr', 'f', 'f2', 'cr', 'cc', 'a') for k in kinds): return None
    pre = []
    def src(o):                                             # value of a source operand
        if o[0] == 'r': return 'cpu_getr(%d, 0x%02x)' % (o[1], o[2])
        if o[0] == 'i': return '0x%xu' % (o[1] & 0xffffffff)
        st, e = ea_code(o)
        if st: pre.append(st)
        return 'cpu_rd(%d, %s)' % (s, e)
    def addr(o):                                            # address of a memory operand, in the temporary ea
        st, e = ea_code(o)
        if st: pre.append(st)
        else: pre.append('ea = %s;' % e)
        return 'ea'
    def done(text): return ' '.join(pre + [text])
    if m == 'ld' and kinds in (('r', 'r'), ('r', 'i'), ('r', 'm')):
        v = src(ops[1]); return done('cpu_setr(%d, 0x%02x, %s);' % (ops[0][1], ops[0][2], v))
    if m == 'ld' and kinds in (('m', 'r'), ('m', 'i')):
        a = addr(ops[0]); return done('cpu_wr(%d, %s, %s);' % (s, a, src(ops[1])))
    if m == 'ld' and kinds == ('m', 'm'):                   # LD (nn),(mem) and LD (mem),(nn): one side is a plain address
        if ops[0][1] == 'abs': v = src(ops[1]); return done('cpu_wr(%d, 0x%06xu, %s);' % (s, ops[0][2], v))
        a = addr(ops[0]); return done('cpu_wr(%d, %s, cpu_rd(%d, 0x%06xu));' % (s, a, s, ops[1][2]))
    if m == 'lda' and kinds == ('r', 'm') and ops[1][1] not in ('dec', 'inc'):
        return 'cpu_setr(%d, 0x%02x, %s);' % (ops[0][1], ops[0][2], ea_code(ops[1])[1])
    if m == 'push' and kinds in (('r',), ('i',)): return 'cpu_push(%d, %s);' % (s if kinds == ('i',) else ops[0][1], src(ops[0]))
    if m == 'push' and kinds == ('m',): v = src(ops[0]); return done('cpu_push(%d, %s);' % (s, v))
    if m == 'pop' and kinds == ('r',): return 'cpu_setr(%d, 0x%02x, cpu_pop(%d));' % (ops[0][1], ops[0][2], ops[0][1])
    if m in ALU and kinds in (('r', 'r'), ('r', 'i'), ('r', 'm')):
        v = src(ops[1]); r = (ops[0][1], ops[0][2])
        call = 'cpu_alu(%d, %d, cpu_getr(%d, 0x%02x), %s)' % (ALU[m], s, r[0], r[1], v)
        return done(call + ';' if m == 'cp' else 'cpu_setr(%d, 0x%02x, %s);' % (r[0], r[1], call))
    if m in ALU and kinds in (('m', 'r'), ('m', 'i')):
        a = addr(ops[0])
        call = 'cpu_alu(%d, %d, cpu_rd(%d, %s), %s)' % (ALU[m], s, s, a, src(ops[1]))
        return done(call + ';' if m == 'cp' else 't = %s; cpu_wr(%d, %s, t);' % (call, s, a))
    if m in ('inc', 'dec') and kinds == ('i', 'r'):
        sz, c, n = ops[1][1], ops[1][2], ops[0][1]
        if sz == 1: return 'cpu_setr(1, 0x%02x, cpu_incdec(%d, 1, cpu_getr(1, 0x%02x), %d));' % (c, m == 'dec', c, n)
        return 'cpu_setr(%d, 0x%02x, cpu_getr(%d, 0x%02x) %s %d);' % (sz, c, sz, c, '-' if m == 'dec' else '+', n)
    if m in ('inc', 'dec') and kinds == ('i', 'm'):
        a = addr(ops[1]); return done('t = cpu_incdec(%d, %d, cpu_rd(%d, %s), %d); cpu_wr(%d, %s, t);' % (m == 'dec', s, s, a, ops[0][1], s, a))
    if m in SHIFT and kinds == ('i', 'r'):                  # count 0 means 16; the count is known here
        n = ops[0][1] or 16; sz, c = ops[1][1], ops[1][2]
        return 'cpu_setr(%d, 0x%02x, cpu_shift(%d, %d, cpu_getr(%d, 0x%02x), %d));' % (sz, c, SHIFT[m], sz, sz, c, n)
    if m in ('extz', 'exts') and kinds == ('r',):
        sz, c = ops[0][1], ops[0][2]; half = 'uint%d_t' % (sz * 4) if m == 'extz' else 'int%d_t' % (sz * 4)
        return 'cpu_setr(%d, 0x%02x, (uint32_t)(%s)cpu_getr(%d, 0x%02x));' % (sz, c, half, sz, c)
    if m == 'ex' and kinds == ('r', 'r'):
        a, b = ops
        return 't = cpu_getr(%d, 0x%02x); cpu_setr(%d, 0x%02x, cpu_getr(%d, 0x%02x)); cpu_setr(%d, 0x%02x, t);' % (a[1], a[2], a[1], a[2], b[1], b[2], b[1], b[2], )
    return None

def gen_function(entry):
    addrs = body(entry)
    aset = set(addrs)
    waits = {}
    for a in addrs:
        d = insn(a)
        if d and d.mnem in ('jr', 'jrl', 'jp', 'djnz'):
            kind, cc, target = T.flow(d)
            if kind == 'branch' and target is not None and target <= a and target in aset:
                k = wait_kind(aset, target, a)
                if k: waits[a] = k; all_waits[a] = k
    labels = set(); lines = []
    def goto(t):
        if t in aset: labels.add(t); return 'goto L_%06x;' % t
        if t in entries: return 'return %s();' % fname(t)
        return 'return ngp_dispatch(0x%06x);' % t
    for idx, a in enumerate(addrs):
        d = insn(a)
        nxt = addrs[idx + 1] if idx + 1 < len(addrs) else None
        L = []
        if not d:
            lines.append((a, ['ngp_fatal(0x%06x, 1); return 0;' % a], 'not an instruction')); continue
        fall = (a + d.length) & 0xffffff
        kind, cc, target = T.flow(d)
        cond = None if cc is None else 'cpu_cond(%d)' % cc if cc != 'djnz' else None
        step = 'STEP(0x%06x);' % a
        ft = True
        if kind is None:
            c = native(d)
            if c: L.append(c); stats['native'] += 1
            else: L.append(step); stats['step'] += 1; step_forms[T.fmt(d).split()[0]] += 1
        elif kind == 'halt': L.append('ngp_wait(0x%06x);' % fall)
        elif kind == 'swi':
            if d.ops[0][1] == 1: L.append('ngp_bios_call(cpu.bank[3][0] >> 8 & 0xff);')
            else: L.append('ngp_fatal(0x%06x, 1); return 0;' % a); ft = False
        elif kind == 'ret':
            pop = 'return cpu_pop(4) & 0xffffff;' if d.mnem == 'ret' and NATIVE else step + ' return cpu.pc;'    # RETD and RETI: by the step
            if cc is None: L.append(pop); ft = False
            else: L.append('if (%s) { %s }' % (cond, pop))
        elif kind in ('jump', 'branch'):
            if d.mnem == 'djnz':
                sz, c = d.ops[0][1], d.ops[0][2]
                L.append('t = (cpu_getr(%d, 0x%02x) - 1) & 0x%x; cpu_setr(%d, 0x%02x, t);' % (sz, c, (1 << (8 * sz)) - 1, sz, c))
                if a in waits: L.append('if (t) { %s(0x%06x); %s }' % ('ngp_wait' if waits[a] == 1 else 'ngp_poll', target, goto(target)))
                else: L.append('if (t) %s' % goto(target))
            elif target is not None:
                if a in waits:
                    L.append('if (%s) { %s(0x%06x); %s }' % (cond, 'ngp_wait' if waits[a] == 1 else 'ngp_poll', target, goto(target)))
                elif cond: L.append('if (%s) %s' % (cond, goto(target)))
                else: L.append(goto(target)); ft = False
            else:                                               # through a register or memory: the traces' targets
                cases = ' '.join('case 0x%06x: %s' % (t, goto(t)) for t in sorted(ind_jump.get(a, ())))
                sw = 'switch (cpu.pc) { %s default: return ngp_dispatch(cpu.pc); }' % cases
                if cond: L.append('%s if (cpu.pc != 0x%06x) %s' % (step, fall, sw))
                else: L.append('%s %s' % (step, sw)); ft = False
        elif kind == 'call':
            ret = fall
            if target is not None:
                callee = '%s()' % fname(target) if target in entries else 'ngp_dispatch(0x%06x)' % target
                c = 'cpu_push(4, 0x%06x); r = %s; if (r != 0x%06x) return r;' % (ret, callee, ret)
                L.append('if (%s) { %s }' % (cond, c) if cond else c)
            else:
                L.append('%s if (cpu.pc != 0x%06x) { r = ngp_dispatch(cpu.pc); if (r != 0x%06x) return r; }' % (step, fall, ret))
        if ft and nxt != fall: L.append(goto(fall))
        lines.append((a, L, T.fmt(d)))
    out = ['uint32_t %s(void)\n{\n    uint32_t r, t, ea; (void)r; (void)t; (void)ea;' % fname(entry)]
    if addrs and addrs[0] != entry: labels.add(entry); out.append('    goto L_%06x;' % entry)
    for a, L, text in lines:
        out.append('%s%s%s' % ('L_%06x: ' % a if a in labels else '    ', ' '.join(L).ljust(60), '   /* %06x %s */' % (a, text)))
    out.append('}\n')
    return '\n'.join(out), len(lines)

HDR = '''/* generated by tools/recomp900.py from the game's ROM: do not edit */
#include "ngp.h"
#define CPU900_EXTERN
#include "cpu900.h"
#include "logic.h"
#include "gen_code.h"
#define STEP(a) do { cpu.pc = (a); cpu_step(); } while (cpu.pc == (a))     /* LDIR and its kind repeat themselves */
'''

os.makedirs(outdir, exist_ok=True)
for f in os.listdir(outdir):
    if f.startswith('gen_') and (f.endswith('.c') or f.endswith('.h')): os.remove(os.path.join(outdir, f))
all_waits = {}; step_forms = collections.Counter()
order = sorted(entries)
files, cur, n = [], [], 0
for e in order:
    text, k = gen_function(e)
    cur.append(text); n += k
    if n >= 2500: files.append(cur); cur, n = [], 0
if cur or not files: files.append(cur)
for i, texts in enumerate(files):
    with open(os.path.join(outdir, 'gen_%02d.c' % i), 'w') as f:
        f.write(HDR + '\n' + '\n'.join(texts))
with open(os.path.join(outdir, 'gen_code.h'), 'w') as f:
    f.write('/* generated by tools/recomp900.py: do not edit */\n#include <stdint.h>\nuint32_t ngp_dispatch(uint32_t pc);\n')
    f.write('\n'.join('uint32_t %s(void);' % fname(e) for e in order) + '\n')
with open(os.path.join(outdir, 'gen_dispatch.c'), 'w') as f:
    f.write(HDR + '\n/* run the code at pc until it returns: gives the address where execution goes on */\n'
            'uint32_t ngp_dispatch(uint32_t pc)\n{\n    if (pc >= 0xff0000) return ngp_bios_entry(pc);\n    switch (pc) {\n')
    for e in order: f.write('    case 0x%06x: return %s();\n' % (e, fname(e)))
    f.write('    }\n    ngp_fatal(pc, 2);\n    return 0;\n}\n')
with open(os.path.join(outdir, 'gen_waits.h'), 'w') as f:
    f.write('/* generated by tools/recomp900.py: the branches back of the loops where the game waits (1 = the frame ends, 2 = poll) */\n'
            'static const struct { uint32_t pc; uint8_t kind; } gen_waits[] = {\n')
    for a in sorted(all_waits): f.write('    { 0x%06x, %d },\n' % (a, all_waits[a]))
    f.write('    { 0, 0 }\n};\n')
with open(os.path.join(outdir, 'gen_game.h'), 'w') as f:
    f.write('/* generated by tools/recomp900.py from tools/games/%s.py: do not edit */\n#define NGP_GEN_FILES %d\n'
            % (gamename or '(none)', len(files)))

decoded = set()
for e in order: decoded |= set(body(e))
missing = sorted(a for a in executed if in_rom(a) and a not in decoded)
total = stats['native'] + stats['step']
print('entries %d, instructions %d in %d files (seen in the traces: %d, traced but not translated: %d), wait loops %d (%d frame, %d poll)'
      % (len(order), len(decoded), len(files), len(executed), len(missing), len(all_waits),
         sum(1 for k in all_waits.values() if k == 1), sum(1 for k in all_waits.values() if k == 2)))
print('non-flow instructions: %d native, %d through the interpreter step (%.0f%%)' % (stats['native'], stats['step'], 100.0 * stats['step'] / max(total, 1)))
if step_forms: print('  most frequent through the step:', ' '.join('%s:%d' % kv for kv in step_forms.most_common(14)))
if missing: print('  traced but not translated, first:', ' '.join('%06x' % a for a in missing[:12]))
if ram_code: print('  WARNING: the traces executed code outside the cartridge (in RAM): %d addresses from %06x. Not translated.' % (len(ram_code), ram_code[0]))
