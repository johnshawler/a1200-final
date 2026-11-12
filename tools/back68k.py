"""back68k.py - the 68020 assembly back end of tools/recomp.py (host only).

The C back end (src/gen_*.c) is what the host test runs; this one writes src/gen68k.s, which the Amiga runs.
Both come from the same decoding of the ROM; equal checksums on the two machines show they agree.

Register use while translated code runs:
  d0 = A, d1 = X, d2 = Y (X and Y are kept zero-extended to 32 bits: they index memory directly)
  d5 = the 6502 V flag (non-zero = set), d4 d6 d7 a0 a1 = scratch
  a2 = NES RAM + $101 + S: a push is move.b x,-(a2), a pull move.b (a2)+,x
  a3 = the PRG bank mapped at $8000, a4 = the fixed bank at $C000, a5 = NES RAM + $80
6502 flags: N and Z are the 68k N and Z, the carry is the 68k X flag (moves, tests and logic leave it alone),
V lives in d5. A liveness analysis of the four flags decides where a flag must be protected.
JSR: move.w #ret,-(a2) / bsr F / dc.w ret. RTS: compare the two bytes it pops with the dc.w after the 68k
return address: equal is the normal return; otherwise the stack was manipulated and _unwind looks for the
68k frame that expects these bytes (or hands the address to the C side).
"""
import os, collections

N, Z, C, V = 1, 2, 4, 8
ALL = 15
NZ = N | Z
REG = {'A': 'd0', 'X': 'd1', 'Y': 'd2'}

USE = collections.defaultdict(int, {'ADC': C, 'SBC': C, 'ROL': C, 'ROR': C, 'PHP': ALL,
       'BPL': N, 'BMI': N, 'BEQ': Z, 'BNE': Z, 'BCC': C, 'BCS': C, 'BVC': V, 'BVS': V})
DEF = collections.defaultdict(int)
for m in 'LDA LDX LDY TAX TAY TXA TYA TSX PLA INX DEX INY DEY INC DEC AND ORA EOR'.split(): DEF[m] = NZ
for m in 'ADC SBC'.split(): DEF[m] = ALL
for m in 'CMP CPX CPY ASL LSR ROL ROR'.split(): DEF[m] = NZ | C
DEF['BIT'] = NZ | V; DEF['CLC'] = C; DEF['SEC'] = C; DEF['CLV'] = V; DEF['PLP'] = ALL; DEF['RTI'] = ALL
BRANCH = {'BPL': 'bpl', 'BMI': 'bmi', 'BEQ': 'beq', 'BNE': 'bne'}
INV = {'bcc': 'bcs', 'bcs': 'bcc', 'beq': 'bne', 'bne': 'beq', 'bpl': 'bmi', 'bmi': 'bpl'}



def generate(g):
    HLE, HLE_FALLBACK = g['game'].HLE, g['game'].HLE_FALLBACK      # hand-written routines: per game (tools/games/)
    entries, body, insn, ctx_of = g['entries'], g['body'], g['insn'], g['ctx_of']
    FIXED, NORETURN, static_branch, successors = g['FIXED'], g['NORETURN'], g['static_branch'], g['successors']
    wait_kind, rd, outdir = g['wait_kind'], g['rd'], g['outdir']
    reset, nmi, irq, dynamic = g['reset'], g['nmi'], g['irq'], g['dynamic_entries']

    def fname(ctx, a): return 'F%d_%04x' % (ctx_of(ctx, a), a)
    banks_for = collections.defaultdict(list)
    for (c, e) in entries:
        if c != FIXED: banks_for[e].append(c)

    def callees(ctx, at, t):
        """the functions a JSR at 'at' to t can reach"""
        if t >= 0xc000: return [(FIXED, t)]
        if t < 0x8000: return []
        if ctx != FIXED and at < 0xc000: return [(ctx, t)] if (ctx, t) in entries else []
        return [(b, t) for b in banks_for[t]]

    # ---------------------------------------------------------------- per function structure
    funcs = {}
    for (ctx, e) in entries:
        addrs = body(ctx, e, True)
        aset = set(addrs)
        info = {}
        prev = None
        for a in addrs:
            d = insn(ctx, a)
            succ, tails = [], []                # local successors; tail calls: (kind, target) out of the function
            if d:
                m, mode, arg, n = d
                fall = (a + n) & 0xffff
                ft, targets, call = successors(ctx, a, prev)
                if m == 'JSR' and arg in NORETURN: ft = False
                if ft: (succ if fall in aset else tails).append(fall)
                for t in targets:
                    (succ if t in aset else tails).append(t)
            info[a] = dict(d=d, succ=succ, tails=tails, prev=prev)
            prev = d
        funcs[(ctx, e)] = dict(ctx=ctx, entry=e, addrs=addrs, aset=aset, info=info)

    def tail_fns(ctx, at, t):
        if t >= 0xc000: return [(FIXED, t)]
        if t < 0x8000: return []
        if ctx != FIXED and at < 0xc000: return [(ctx, t)]
        return [(b, t) for b in banks_for[t]]

    # ---------------------------------------------------------------- must_def: flags every return path defines
    must = {k: ALL for k in funcs}
    changed = True
    while changed:
        changed = False
        for k, f in funcs.items():
            ctx = f['ctx']
            din = {f['entry']: 0}
            order = f['addrs']
            exits = []
            # iterate forward to a fixpoint (intersection)
            dout = {}
            work = True
            while work:
                work = False
                for a in order:
                    if a not in din: continue
                    i = f['info'][a]; d = i['d']
                    if not d: exits.append(0); continue
                    m, mode, arg, n = d
                    o = din[a] | DEF[m]
                    if m == 'JSR':
                        cs = callees(ctx, a, arg)
                        mm = ALL if cs else 0
                        for c in cs: mm &= must.get(c, 0)
                        o = din[a] | mm
                    if dout.get(a) != o: dout[a] = o
                    for s in i['succ']:
                        nv = din[s] & o if s in din else o
                        if din.get(s) != nv: din[s] = nv; work = True
            res = ALL
            for a in order:
                if a not in dout: continue
                i = f['info'][a]; d = i['d']
                if not d: res = 0; continue
                m, mode, arg, n = d
                if m in ('RTS',): res &= dout[a]
                elif m in ('RTI',): res &= ALL
                elif (m == 'JMP' and mode == 'ind') or (m == 'JSR' and arg in NORETURN): res &= dout[a]
                for t in i['tails']:
                    mm = ALL
                    for c in tail_fns(ctx, a, t): mm &= must.get(c, 0)
                    if not tail_fns(ctx, a, t): mm = 0
                    res &= dout[a] | mm
            if res != must[k]: must[k] = res; changed = True

    # ---------------------------------------------------------------- liveness, with exit liveness from callers
    exit_live = {k: 0 for k in funcs}
    entry_live = {k: 0 for k in funcs}
    for k in funcs:
        if k in dynamic: exit_live[k] = ALL
    live_out_all = {}
    changed = True
    while changed:
        changed = False
        contrib = collections.defaultdict(int)
        for k, f in funcs.items():
            ctx = f['ctx']
            lin, lout = {}, {}
            work = True
            while work:
                work = False
                for a in reversed(f['addrs']):
                    i = f['info'][a]; d = i['d']
                    if not d: lin[a] = 0; lout[a] = 0; continue
                    m, mode, arg, n = d
                    o = 0
                    for s in i['succ']: o |= lin.get(s, 0)
                    for t in i['tails']:
                        tf = tail_fns(ctx, a, t)
                        if not tf: o |= ALL
                        for c in tf: o |= entry_live.get(c, ALL)
                    if m == 'RTS': inn = exit_live[k]
                    elif m == 'RTI': inn = 0
                    elif m == 'JMP' and mode == 'ind': inn = ALL
                    elif m == 'JSR':
                        cs = callees(ctx, a, arg)
                        if arg in NORETURN: o = ALL
                        if not cs: inn = ALL
                        else:
                            el, md = 0, ALL
                            for c in cs: el |= entry_live.get(c, ALL); md &= must.get(c, 0)
                            inn = el | (o & ~md)
                    elif m == 'BRK': inn = 0
                    else: inn = USE[m] | (o & ~DEF[m])
                    if lin.get(a) != inn or lout.get(a) != o:
                        lin[a] = inn; lout[a] = o; work = True
            el = lin.get(f['entry'], 0)
            if el != entry_live[k]: entry_live[k] = el; changed = True
            # call sites tell their callees what is live after the call
            for a in f['addrs']:
                d = f['info'][a]['d']
                if d and d[0] == 'JSR' and d[2] not in NORETURN:
                    for c in callees(ctx, a, d[2]): contrib[c] |= lout[a]
                for t in f['info'][a]['tails']:
                    for c in tail_fns(ctx, a, t): contrib[c] |= exit_live[k]
            live_out_all[k] = lout
        for c, v in contrib.items():
            if c in exit_live and (exit_live[c] | v) != exit_live[c]:
                exit_live[c] |= v; changed = True

    # ---------------------------------------------------------------- emission
    out = []
    dynstubs = {}
    def E(s): out.append(s)

    def call_target(ctx, at, t):
        """label for bsr/jmp to the code at t"""
        if t >= 0xc000: return fname(FIXED, t)
        if t < 0x8000: return dynstub(t)
        if ctx != FIXED and at < 0xc000 and (ctx, t) in entries: return fname(ctx, t)
        return dynstub(t)

    def dynstub(t):
        if t not in dynstubs: dynstubs[t] = 'D_%04x' % t
        return dynstubs[t]

    def operand(mode, arg, s0=False):
        """(pre lines, operand, pre clobbers N/Z) for reading a memory operand"""
        if s0 and mode == 'abs' and arg == 0x2002: return ['bsr _rd_s0'], 'd4', False
        if mode == 'imm': return [], '#$%02x' % arg, False
        if mode == 'zp': return [], '%d(a5)' % (arg - 0x80), False
        if mode == 'zpx': return [], '%d(a5,d1.w)' % (arg - 0x80), False
        if mode == 'zpy': return [], '%d(a5,d2.w)' % (arg - 0x80), False
        if mode == 'abs':
            if arg < 0x2000: return [], '%d(a5)' % ((arg & 0x7ff) - 0x80), False
            if arg >= 0xc000: return [], '%d(a4)' % (arg - 0xc000), False
            if arg >= 0x8000: return [], '%d(a3)' % (arg - 0x8000), False
            return ['lea $%04x.w,a0' % arg, 'bsr _io_rd'], 'd4', False
        if mode in ('abx', 'aby'):
            r = 'd1' if mode == 'abx' else 'd2'
            # a RAM table: the index never runs past $7FF (measured over the traces, see NOTES.md)
            if arg < 0x2000 and (arg & 0x7ff) + 255 < 0x900: return [], '(%d,a5,%s.w)' % ((arg & 0x7ff) - 0x80, r), False
            if arg >= 0xc000 and arg + 255 <= 0xffff: return [], '(%d,a4,%s.w)' % (arg - 0xc000, r), False
            if arg >= 0x8000 and arg + 255 < 0xc000: return [], '(%d,a3,%s.w)' % (arg - 0x8000, r), False
            return ['movea.l #$%04x,a0' % arg, 'adda.l %s,a0' % r, 'bsr _rd_ea'], 'd4', True
        if mode == 'izy':
            return ['moveq #0,d7', 'move.w %d(a5),d7' % (arg - 0x80), 'rol.w #8,d7', 'movea.l d7,a0', 'adda.l d2,a0', 'bsr _rd_ea'], 'd4', True
        if mode == 'izx':
            return ['lea %d(a5,d1.w),a0' % (arg - 0x80), 'moveq #0,d7', 'move.w (a0),d7', 'rol.w #8,d7', 'movea.l d7,a0', 'bsr _rd_ea'], 'd4', True
        raise ValueError(mode)

    def store(mode, arg, reg):
        """(lines, clobbers N/Z with something other than reg)"""
        if mode in ('zp', 'zpx', 'zpy') or (mode == 'abs' and arg < 0x2000) or (mode in ('abx', 'aby') and arg < 0x2000 and (arg & 0x7ff) + 255 < 0x900):
            pre, op, _ = operand(mode, arg)
            return pre + ['move.b %s,%s' % (reg, op)], False
        if mode == 'abs':
            return ['lea $%04x.w,a0' % arg if arg < 0x8000 else 'movea.l #$%04x,a0' % arg, 'bsr _io_wr_%s' % reg], None
        if mode in ('abx', 'aby'):
            r = 'd1' if mode == 'abx' else 'd2'
            return ['movea.l #$%04x,a0' % arg, 'adda.l %s,a0' % r, 'bsr _wr_ea_%s' % reg], None
        if mode == 'izy':
            return ['moveq #0,d7', 'move.w %d(a5),d7' % (arg - 0x80), 'rol.w #8,d7', 'movea.l d7,a0', 'adda.l d2,a0', 'bsr _wr_ea_%s' % reg], True
        if mode == 'izx':
            return ['lea %d(a5,d1.w),a0' % (arg - 0x80), 'moveq #0,d7', 'move.w (a0),d7', 'rol.w #8,d7', 'movea.l d7,a0', 'bsr _wr_ea_%s' % reg], True
        raise ValueError(mode)

    stubcount = [0]
    def newstub():
        stubcount[0] += 1
        return '.s%d' % stubcount[0]

    for k in sorted(funcs):
        f = funcs[k]; ctx = f['ctx']; e = f['entry']; addrs = f['addrs']; aset = f['aset']
        lout = live_out_all[k]
        # labels
        labels = set()
        for idx, a in enumerate(addrs):
            d = f['info'][a]['d']
            nxt = addrs[idx + 1] if idx + 1 < len(addrs) else None
            fall = (a + d[3]) & 0xffff if d else None
            for s in f['info'][a]['succ']:
                if s != fall or nxt != fall or (d and d[0] == 'JMP') or (d and d[1] == 'rel' and s == d[2]):
                    labels.add(s)
        labels.add(e)
        stubs = []
        E('    xdef %s' % fname(ctx, e))         # exported only so a profiler can name them
        E('%s:' % fname(ctx, e))
        if (ctx, e) in HLE:
            E('; hand-written equivalent of the 6502 routine')
            for l in HLE[(ctx, e)].strip('\n').split('\n'): E(l)
            for l in ['move.w ccr,d6', 'movea.l (sp)+,a0', 'cmpm.w (a2)+,(a0)+', 'bne _unwind', 'move.w d6,ccr', 'jmp (a0)']: E('    ' + l)
            if (ctx, e) not in HLE_FALLBACK: continue
            E('.orig:')
        if addrs and addrs[0] != e: E('    bra .L%04x' % e)
        nzsrc = None; c68 = None; known_c = None; fuse = None
        # waits
        waits = {}
        for a in addrs:
            d = f['info'][a]['d']
            if d and d[1] == 'rel' and d[2] <= a and d[2] in aset:
                kd = wait_kind(ctx, d[2], a)
                if kd in ('RAM', 'VBL'): waits[a] = (kd, d[2])
        s0 = set()
        for a in addrs:
            d = f['info'][a]['d']
            if d and d[1] == 'rel' and d[2] <= a and d[2] in aset and wait_kind(ctx, d[2], a) == 'S0':
                for x in addrs:
                    if d[2] <= x <= a: s0.add(x)

        def goto(t, a):
            if t in aset: return '.L%04x' % t
            return call_target(ctx, a, t)

        for idx, a in enumerate(addrs):
            i = f['info'][a]; d = i['d']
            nxt = addrs[idx + 1] if idx + 1 < len(addrs) else None
            if a in labels:
                E('.L%04x:' % a)
                nzsrc = None; c68 = None; known_c = None; fuse = None
            if not d:
                E('    move.w #$%04x,d7' % a); E('    bra _fatal_op'); continue
            m, mode, arg, n = d
            LO = lout.get(a, ALL)
            fall = (a + n) & 0xffff
            L = []
            keep_c = False

            def protect_nz(lines, clob_reg):
                """emit lines that clobber N/Z (setting them from clob_reg, or None = garbage) while N/Z must survive"""
                if not (LO & NZ & ~DEF[m]) or clob_reg == nzsrc and clob_reg is not None:
                    return lines
                if nzsrc is not None and nzsrc in ('d0', 'd1', 'd2'):
                    return lines + ['tst.b %s' % nzsrc]
                return ['move.w ccr,d6'] + lines + ['move.w d6,ccr']

            if m in ('LDA', 'LDX', 'LDY'):
                r = REG[m[2]]
                pre, op, _ = operand(mode, arg, a in s0)
                L += pre + ['move.b %s,%s' % (op, r)]
                nzsrc = r; c68 = None; keep_c = True
            elif m in ('STA', 'STX', 'STY'):
                r = REG[m[2]]
                lines, clob = store(mode, arg, r)
                if clob is None: L += lines                         # helper: the flags stay as they were
                else:
                    L += protect_nz(lines, None if clob else r)
                    c68 = None
                    if not (LO & NZ): nzsrc = None
                keep_c = True
            elif m in ('TAX', 'TAY', 'TXA', 'TYA'):
                src, dst = REG[m[1]], REG[m[2]]
                L.append('move.b %s,%s' % (src, dst)); nzsrc = dst; c68 = None; keep_c = True
            elif m == 'TSX':
                L += ['movea.l a2,a0', 'suba.l a5,a0', 'lea -$81(a0),a0', 'move.l a0,d1', 'tst.b d1']; nzsrc = 'd1'; c68 = None; keep_c = True
            elif m == 'TXS':
                L.append('lea ($81,a5,d1.w),a2'); keep_c = True
            elif m == 'PHA':
                L += protect_nz(['move.b d0,-(a2)'], 'd0'); keep_c = True; c68 = None
                if not (LO & NZ): nzsrc = None
            elif m == 'PLA':
                L.append('move.b (a2)+,d0'); nzsrc = 'd0'; c68 = None; keep_c = True
            elif m == 'PHP':
                L.append('bsr _php'); keep_c = True
            elif m == 'PLP':
                L.append('bsr _plp'); nzsrc = None; c68 = None
            elif m in ('ORA', 'AND'):
                pre, op, _ = operand(mode, arg)
                L += pre + ['%s.b %s,d0' % ('or' if m == 'ORA' else 'and', op)]; nzsrc = 'd0'; c68 = None; keep_c = True
            elif m == 'EOR':
                pre, op, _ = operand(mode, arg)
                if op.startswith('#'): L += pre + ['eori.b %s,d0' % op]
                elif op == 'd4': L += pre + ['eor.b d4,d0']
                else: L += pre + ['move.b %s,d4' % op, 'eor.b d4,d0']
                nzsrc = 'd0'; c68 = None; keep_c = True
            elif m == 'ADC':
                pre, op, _ = operand(mode, arg)
                L += pre
                if known_c == 0:
                    L.append('add.b %s,d0' % op)
                else:
                    if op != 'd4': L.append('move.b %s,d4' % op)
                    L += ['ori.b #$04,ccr', 'addx.b d4,d0']
                if LO & V: L.append('svs d5')
                nzsrc = 'd0'; c68 = 'same'
            elif m == 'SBC':
                pre, op, _ = operand(mode, arg)
                L += pre
                if known_c == 1:
                    L.append('sub.b %s,d0' % op)
                else:
                    if op != 'd4': L.append('move.b %s,d4' % op)
                    L += ['eori.b #$10,ccr', 'ori.b #$04,ccr', 'subx.b d4,d0']
                if LO & V: L.append('svs d5')
                L.append('eori.b #$10,ccr')
                nzsrc = 'd0'; c68 = 'inv'
            elif m in ('CMP', 'CPX', 'CPY'):
                r = {'CMP': 'd0', 'CPX': 'd1', 'CPY': 'd2'}[m]
                pre, op, _ = operand(mode, arg)
                L += pre
                nd = insn(ctx, nxt) if nxt is not None else None
                can_fuse = (nxt == fall and nxt not in labels and nd and nd[0] in ('BCC', 'BCS')
                            and not (lout.get(nxt, ALL) & C) and nxt not in waits)
                if not (LO & C) or can_fuse:
                    L.append('cmp.b %s,%s' % (op, r)); c68 = 'inv'; keep_c = True
                    if can_fuse: fuse = nxt
                else:
                    L += ['move.b %s,d4' % r, 'sub.b %s,d4' % op, 'eori.b #$10,ccr']; c68 = 'inv'
                nzsrc = None
            elif m == 'BIT':
                pre, op, _ = operand(mode, arg, a in s0)
                L += pre
                if op != 'd4': L.append('move.b %s,d4' % op)
                if LO & V: L += ['move.b d4,d5', 'andi.b #$40,d5']
                if (LO & N) and (LO & Z):
                    sk = newstub()
                    L += ['move.b d0,d7', 'and.b d4,d7', 'move.w ccr,d7', 'andi.b #$f7,d7', 'tst.b d4', 'bpl.s %sb' % sk,
                          'ori.b #$08,d7', '%sb:' % sk, 'move.w d7,ccr']
                elif LO & Z: L += ['move.b d0,d7', 'and.b d4,d7']
                elif LO & N: L.append('tst.b d4')
                nzsrc = None; c68 = None; keep_c = True
            elif m in ('ASL', 'LSR', 'ROL', 'ROR'):
                opc = {'ASL': 'add.b {r},{r}', 'LSR': 'lsr.b #1,{r}', 'ROL': 'roxl.b #1,{r}', 'ROR': 'roxr.b #1,{r}'}[m]
                if mode == 'acc':
                    L.append(opc.format(r='d0')); nzsrc = 'd0'; c68 = 'same'
                else:
                    pre, op, _ = operand(mode, arg)
                    if op == 'd4': L += pre + [opc.format(r='d4'), 'bsr _wr_ea_d4']     # a0 still holds the address
                    else: L += pre + ['move.b %s,d4' % op, opc.format(r='d4'), 'move.b d4,%s' % op]
                    nzsrc = None; c68 = None
            elif m in ('INC', 'DEC'):
                pre, op, _ = operand(mode, arg)
                tab = '_inctab' if m == 'INC' else '_dectab'
                if op == 'd4':                      # generic address in a0, value in d4
                    if LO & C: L += pre + ['and.w #$ff,d4', 'move.b (%s,d4.w),d4' % tab, 'bsr _wr_ea_d4']; keep_c = True
                    else: L += pre + ['%s.b #1,d4' % ('addq' if m == 'INC' else 'subq'), 'bsr _wr_ea_d4']
                elif LO & C:
                    L += pre + ['moveq #0,d4', 'move.b %s,d4' % op, 'move.b (%s,d4.w),d4' % tab, 'move.b d4,%s' % op]
                    keep_c = True
                else:
                    L += pre + ['%s.b #1,%s' % ('addq' if m == 'INC' else 'subq', op)]
                nzsrc = None; c68 = None
            elif m in ('INX', 'DEX', 'INY', 'DEY'):
                r = REG[m[2]]
                if LO & C:
                    L.append('move.b (%s,%s.w),%s' % ('_inctab' if m[0] == 'I' else '_dectab', r, r)); keep_c = True; c68 = None
                else:
                    L.append('%s.b #1,%s' % ('addq' if m[0] == 'I' else 'subq', r)); c68 = None
                nzsrc = r
            elif m == 'CLC':
                L.append('andi.b #$ef,ccr'); known_c = 0; keep_c = 'set'
            elif m == 'SEC':
                L.append('ori.b #$10,ccr'); known_c = 1; keep_c = 'set'
            elif m == 'CLV':
                L.append('sf d5'); keep_c = True
            elif m in ('SEI', 'CLI'):
                L.append('%s _cpu_i' % ('st' if m == 'SEI' else 'sf')); keep_c = True
            elif m in ('SED', 'CLD'):
                L.append('%s _cpu_d' % ('st' if m == 'SED' else 'sf')); keep_c = True
            elif m == 'NOP':
                keep_c = True
            elif mode == 'rel':
                sb = static_branch(ctx, a, i['prev'])
                tgt = goto(arg, a)
                after = LO                          # live on both paths
                if a in waits:
                    kd, lp = waits[a]
                    st = newstub()
                    stubs.append((st, ['lea $%04x.l,a1' % lp, 'bsr _wait_%s' % kd.lower(), 'bra .L%04x' % lp]))
                    tgt = st
                if sb is True:
                    L.append('bra %s' % tgt)
                elif sb is False:
                    pass
                else:
                    if m in BRANCH:
                        L.append('%s %s' % (BRANCH[m], tgt))
                    elif m in ('BCC', 'BCS'):
                        cc = 'bcs' if m == 'BCS' else 'bcc'
                        if fuse == a or c68 == 'inv':
                            L.append('%s %s' % (INV[cc], tgt))
                        elif c68 == 'same':
                            L.append('%s %s' % (cc, tgt))
                        else:
                            if after & NZ:
                                st = newstub()
                                stubs.append((st, ['move.w d6,ccr', 'bra %s' % tgt]))
                                L += ['move.w ccr,d6', 'moveq #0,d7', 'negx.b d7', '%s %s' % (cc, st), 'move.w d6,ccr']
                            else:
                                L += ['moveq #0,d7', 'negx.b d7', '%s %s' % (cc, tgt)]
                                nzsrc = None; c68 = 'same'
                    else:                           # BVC BVS
                        cc = 'bne' if m == 'BVS' else 'beq'
                        if after & NZ:
                            st = newstub()
                            stubs.append((st, ['move.w d6,ccr', 'bra %s' % tgt]))
                            L += ['move.w ccr,d6', 'tst.b d5', '%s %s' % (cc, st), 'move.w d6,ccr']
                        else:
                            L += ['tst.b d5', '%s %s' % (cc, tgt)]
                            nzsrc = None; c68 = None
                keep_c = True
                fuse = None
            elif m == 'JMP':
                if mode == 'abs':
                    L.append(('bra %s' if arg in aset else 'jmp %s') % goto(arg, a))
                else:
                    hi = (arg & 0xff00) | ((arg + 1) & 0xff)
                    L.append('move.w ccr,d6')
                    if arg < 0x2000:
                        L += ['move.b %d(a5),d7' % ((hi & 0x7ff) - 0x80), 'lsl.w #8,d7', 'move.b %d(a5),d7' % ((arg & 0x7ff) - 0x80)]
                    else:
                        L += ['lea $%04x.w,a0' % hi, 'bsr _rd_ea', 'move.b d4,d7', 'lsl.w #8,d7', 'lea $%04x.w,a0' % arg, 'bsr _rd_ea', 'move.b d4,d7']
                    L.append('jmp _dispatch')
            elif m == 'JSR':
                ret = (a + 2) & 0xffff
                sw = ((ret & 0xff) << 8) | (ret >> 8)
                cs = callees(ctx, a, arg)
                el = 0
                for c in cs: el |= entry_live.get(c, ALL)
                if not cs: el = ALL
                push = ['move.w #$%04x,-(a2)' % sw]
                if el & NZ: push = ['move.w ccr,d6'] + push + ['move.w d6,ccr']
                L += push + ['bsr %s' % call_target(ctx, a, arg), 'dc.w $%04x' % sw]
                nzsrc = None; c68 = None
            elif m == 'RTS':
                if exit_live[k] & NZ: L += ['move.w ccr,d6', 'movea.l (sp)+,a0', 'cmpm.w (a2)+,(a0)+', 'bne _unwind', 'move.w d6,ccr', 'jmp (a0)']
                else: L += ['movea.l (sp)+,a0', 'cmpm.w (a2)+,(a0)+', 'bne _unwind_x', 'jmp (a0)']
            elif m == 'RTI':
                L += ['move.b (a2)+,d4', 'move.w (a2)+,d7', 'rol.w #8,d7', 'bsr _setp', 'bra _rti']
            elif m == 'BRK':
                L += ['move.w #$%04x,d7' % a, 'bra _fatal_op']
            else:
                raise ValueError(m)
            if keep_c is not True and keep_c != 'set': known_c = None
            # fall through into an instruction that is not the next one written
            ft = bool(i['succ'] and fall in i['succ']) or (fall in i['tails'])
            if ft and m not in ('JMP', 'RTS', 'RTI', 'BRK') and not (m == 'JSR' and arg in NORETURN) and nxt != fall:
                if mode == 'rel' and static_branch(ctx, a, i['prev']) is True: pass
                elif fall in aset: L.append('bra .L%04x' % fall)
                else: L.append('jmp %s' % call_target(ctx, a, fall))
            E('; %04x %s' % (a, m))
            for l in L:
                E(l if l.endswith(':') else '    ' + l)
        for st, lines in stubs:
            E('%s:' % st)
            for l in lines: E('    ' + l)

    # dynamic call stubs: the bank decides which function a call into $8000-$BFFF reaches
    for t, lab in sorted(dynstubs.items()):
        E('%s:' % lab)
        E('    move.w ccr,d6')
        if t >= 0x8000:
            E('    move.b _nes_bank,d7')
            for b in sorted(banks_for[t]):
                E('    cmp.b #%d,d7' % b)
                E('    bne.s .n%d' % b)
                E('    move.w d6,ccr')
                E('    jmp %s' % fname(b, t))
                E('.n%d:' % b)
        E('    move.w #$%04x,d7' % t)
        E('    bra _dispatch')

    # the run-time helpers, in this same code section
    E('    include "src/nes68k.s"')
    # dispatch: an open-addressing hash of (bank, address) -> function. Bank 7 for $C000-$FFFF.
    E('    section gendata,data')
    E('    cnop 0,4')
    HS = 2048
    slots = [None] * HS
    for (c, e) in sorted(entries):
        h = (e + c * 311) & (HS - 1)
        while slots[h] is not None: h = (h + 1) & (HS - 1)
        slots[h] = (c, e)
    E('_disp_hash:')
    for sl in slots:
        E('    dc.w 0,0,0,0' if sl is None else '    dc.w $%04x,%d\n    dc.l %s' % (sl[1], sl[0], fname(sl[0], sl[1])))
    E('_reset_fn: dc.l %s' % fname(FIXED, reset))
    E('_nmi_fn: dc.l %s' % fname(FIXED, nmi))

    with open(os.path.join(outdir, 'gen68k.s'), 'w') as fo:
        fo.write('; generated by tools/recomp.py (back68k.py) from the game ROM: do not edit\n')
        fo.write('    section gencode,code\n')
        fo.write('    xdef _disp_hash,_reset_fn,_nmi_fn\n')
        fo.write('\n'.join(out) + '\n')
    return dict(must=must, exit_live=exit_live, entry_live=entry_live)
