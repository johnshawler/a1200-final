#!/usr/bin/env python3
"""asm900.py - a small TLCS-900/H assembler (host only), on the tables of tools/tlcs900.py.

usage: asm900.py source.asm output.bin [-l listing.txt]

It exists so that test cartridges for the tools can be built here, without a commercial ROM and without an
outside assembler. A port never needs it. The syntax is the one tools/dis900.py prints:

    label:  ld    wa,0x1234         ; registers and mnemonics in either case, labels as written
            ld    (xhl+),a          ; (r+) and (-r) step by the operand size; write (xhl+:2) to say it
            ldw   (0x6fcc),vbl      ; no register operand: the suffix gives the width (none or b, w, l)
            jr    nz,label          ; jr = 8-bit, jrl = 16-bit displacement; jp and call take any address
            cp    a,(xix+4)         ; (r32), (r32+d), (r32+r8), (r32+r16), (-r32), (r32+), (address)

Directives: org, equ (name equ value, or name = value), db (bytes, "strings"), dw, dl, ds count[,fill],
align n, incbin "file", include "file". Expressions are Python expressions over the symbols; $ is the address
of the current statement. The output starts at the first org.
"""
import sys, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tlcs900 as T

class AsmError(Exception): pass

CC_BY_NAME = dict((n, i) for i, n in enumerate(T.CC)); CC_BY_NAME.update(T.CC_ALIAS)
CC_MNEMS = ('jp', 'jr', 'jrl', 'call', 'ret', 'scc')
SHIFTS = ('rlc', 'rrc', 'rl', 'rr', 'sla', 'sra', 'sll', 'srl')
ALL_MNEMS = set(e[0] for t in (T.MAIN, T.REG, T.SRC, T.DST) for e in t.values()) | {'di', 'ldar'}
UNKNOWN = 0xffffff                                          # value of a symbol that is not defined yet

def split_ops(text):
    """split at commas that are not inside parentheses or quotes"""
    out, depth, cur, q = [], 0, '', None
    for ch in text:
        if q:
            cur += ch
            if ch == q: q = None
        elif ch == '"' or (ch == "'" and not cur[-1:].isalnum()): q = ch; cur += ch     # XWA' is a register, not a quote
        elif ch == '(': depth += 1; cur += ch
        elif ch == ')': depth -= 1; cur += ch
        elif ch == ',' and depth == 0: out.append(cur.strip()); cur = ''
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

class Assembler(object):
    def __init__(self):
        self.sym = {}
        self.mem = {}
        self.base = None
        self.listing = []

    # ---- expressions
    def ev(self, text, pc):
        env = dict(self.sym_pass)
        env['__pc__'] = pc
        text = text.replace('$', '__pc__')
        names = re.findall(r'[A-Za-z_.][A-Za-z_0-9.]*', re.sub(r'0[xXbB][0-9A-Fa-f]+|"[^"]*"|\'[^\']*\'', '', text))
        for n in names:
            if n not in env and n not in ('__pc__', 'and', 'or', 'not'):
                if self.final: raise AsmError('unknown symbol %s' % n)
                env[n] = UNKNOWN; self.unknown = True
        try: return int(eval(text.replace('.', '_DOT_'), {'__builtins__': {}, 'ord': ord, 'len': len},
                             dict((k.replace('.', '_DOT_'), v) for k, v in env.items())))
        except AsmError: raise
        except Exception as e: raise AsmError('bad expression "%s": %s' % (text, e))

    # ---- operands
    def operand(self, text, pc, mnem, index, count, size):
        up = text.upper()
        if mnem in CC_MNEMS and index == 0 and up in CC_BY_NAME and (count == 2 or mnem == 'ret'):
            return ('cc', CC_BY_NAME[up])
        if up == 'SR': return ('sr',)
        if up == 'F': return ('f',)
        if up == "F'": return ('f2',)
        if up in T.REG_BY_NAME: s, c = T.REG_BY_NAME[up]; return ('r', s, c)
        if mnem == 'ldc' and re.match(r'^CR[0-9A-F]{2}$', up): return ('i', int(up[2:], 16), False)
        if text.startswith('(') and text.endswith(')'):
            inner = text[1:-1].strip()
            m = re.match(r'^-\s*([A-Za-z0-9\']+)\s*(?::\s*(\d))?$', inner)
            if m and m.group(1).upper() in T.REG_BY_NAME:
                s, c = T.REG_BY_NAME[m.group(1).upper()]
                if s != 4: raise AsmError('address register must be 32-bit: %s' % text)
                return ('m', 'dec', c, int(m.group(2) or size or 1))
            m = re.match(r'^([A-Za-z0-9\']+)\s*\+\s*(?::\s*(\d))?$', inner)
            if m and m.group(1).upper() in T.REG_BY_NAME:
                s, c = T.REG_BY_NAME[m.group(1).upper()]
                if s != 4: raise AsmError('address register must be 32-bit: %s' % text)
                return ('m', 'inc', c, int(m.group(2) or size or 1))
            m = re.match(r'^([A-Za-z0-9\']+)\s*(?:([+-])\s*(.+))?$', inner)
            if m and m.group(1).upper() in T.REG_BY_NAME and T.REG_BY_NAME[m.group(1).upper()][0] == 4:
                c = T.REG_BY_NAME[m.group(1).upper()][1]
                if not m.group(2): return ('m', 'ind', c)
                rest = m.group(3).strip()
                if rest.upper() in T.REG_BY_NAME and m.group(2) == '+':
                    s2, c2 = T.REG_BY_NAME[rest.upper()]
                    if s2 == 4: raise AsmError('index register must be 8 or 16 bits: %s' % text)
                    return ('m', 'idx', c, c2, s2)
                d = self.ev(rest, pc)
                return ('m', 'disp', c, -d if m.group(2) == '-' else d)
            force = 0
            m = re.match(r'^(.*):\s*(8|16|24)$', inner)
            if m: inner, force = m.group(1), int(m.group(2))
            self.unknown = False
            v = self.ev(inner, pc)
            return ('m', 'abs', v & 0xffffff, force or (24 if self.unknown else 0))
        self.unknown = False
        v = self.ev(text, pc)
        return ('i', v, self.unknown)

    # ---- encoding
    def prefix_reg(self, zz, code, size):
        short = T.CODE8 if size == 1 else T.CODE16
        if code in short: return [0xc8 + zz * 16 + short.index(code)]
        return [0xc7 + zz * 16, code]

    def prefix_mem(self, zz, op, pc):
        k = op[1]
        if k == 'ind':
            if op[2] in T.CODE16: return [0x80 + zz * 16 + T.CODE16.index(op[2])]
            return [0xc3 + zz * 16, op[2]]
        if k == 'disp':
            d = op[3]
            if op[2] in T.CODE16 and -128 <= d <= 127: return [0x88 + zz * 16 + T.CODE16.index(op[2]), d & 0xff]
            if not -32768 <= d <= 32767: raise AsmError('displacement out of range')
            return [0xc3 + zz * 16, op[2] | 1, d & 0xff, (d >> 8) & 0xff]
        if k == 'abs':
            v, force = op[2], op[3]
            if force == 8 or (force == 0 and v < 0x100): return [0xc0 + zz * 16, v & 0xff]
            if force == 16 or (force == 0 and v < 0x10000): return [0xc1 + zz * 16, v & 0xff, (v >> 8) & 0xff]
            return [0xc2 + zz * 16, v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff]
        if k == 'idx': return [0xc3 + zz * 16, 0x03 if op[4] == 1 else 0x07, op[2], op[3]]
        if k in ('dec', 'inc'):
            if op[3] not in (1, 2, 4): raise AsmError('step must be 1, 2 or 4')
            return [(0xc4 if k == 'dec' else 0xc5) + zz * 16, op[2] | {1: 0, 2: 1, 4: 2}[op[3]]]
        raise AsmError('cannot encode memory operand')

    def imm_bytes(self, v, size, signed_ok=True):
        lo, hi = (-(1 << (8 * size - 1)) if signed_ok else 0), (1 << (8 * size)) - 1
        if not lo <= v <= hi:
            if self.final: raise AsmError('value 0x%x does not fit in %d byte(s)' % (v, size))
            v = 0
        return [(v >> (8 * i)) & 0xff for i in range(size)]

    def try_entry(self, table, op, entry, ops, pc, suffix):
        """bytes for this table entry with these operands, or None when it does not fit"""
        mnem, sizes, kinds = entry
        if len(kinds) != len(ops): return None
        size = 0
        pfx = None
        if table is T.MAIN or table is T.DST:
            size = int(sizes) if sizes.isdigit() else 0
            has_reg = any(k in ('R8', 'R16', 'R32', 'A', 'SR', 'F') for k in kinds)
            if size and not has_reg and size != (suffix or 1): return None
            if suffix and size and has_reg and suffix != size: return None
            if table is T.DST: zz = 3
        elif table is T.REG:
            i = kinds.index('r') if 'r' in kinds else kinds.index('rr')
            o = ops[i]
            if o[0] != 'r': return None
            size = o[1] if 'r' in kinds else o[1] // 2
            if size not in (1, 2, 4) or 'bwl'[{1: 0, 2: 1, 4: 2}[size]] not in sizes: return None
            if suffix and suffix != size: return None
            zz = {1: 0, 2: 1, 4: 2}[size]
            code = o[2]
            if 'rr' in kinds and size == 1 and code in T.CODE16[:4]:    # MUL WA,#: the short form names the low byte
                pfx = [0xc8 + T.CODE16.index(code) * 2 + 1]
            else: pfx = self.prefix_reg(zz, code, size)
        else:                                                           # SRC
            size = suffix or 1
            for k, o in zip(kinds, ops):
                if k == 'R' and o[0] == 'r': size = o[1]
                if k == 'RR' and o[0] == 'r': size = o[1] // 2
            if size not in (1, 2, 4) or 'bwl'[{1: 0, 2: 1, 4: 2}[size]] not in sizes: return None
            if suffix and suffix != size: return None
            zz = {1: 0, 2: 1, 4: 2}[size]
        field, mask, extra, rel = 0, 0, [], None
        for k, o in zip(kinds, ops):
            t = o[0]
            if k == 'r' or k == 'rr':
                if t != 'r': return None
            elif k == 'M':
                if t != 'm': return None
                pfx = self.prefix_mem(zz, o, pc)
            elif k in ('R', 'R8', 'R16', 'R32'):
                want = size if k == 'R' else int(k[1:]) // 8
                if t != 'r' or o[1] != want: return None
                short = T.CODE8 if want == 1 else T.CODE16
                if o[2] not in short: return None
                field, mask = short.index(o[2]), 7
            elif k == 'RR':
                if t != 'r' or o[1] != size * 2 or o[2] not in T.CODE16: return None
                i = T.CODE16.index(o[2])
                if size == 1:
                    if i > 3: return None
                    field, mask = i * 2 + 1, 7
                else: field, mask = i, 7
            elif k in ('I', 'I8', 'I16', 'I32'):
                if t != 'i': return None
                extra += self.imm_bytes(o[1], size if k == 'I' else int(k[1:]) // 8)
            elif k in ('N3', 'Q3'):
                lo = 1 if k == 'N3' else 0
                if t != 'i' or not lo <= o[1] <= lo + 7: return None
                field, mask = o[1] & 7, 7
            elif k == 'N4':
                if t != 'i' or not 0 <= o[1] <= 15: return None
                extra.append(o[1])
            elif k == 'CR':
                if t != 'i' or not 0 <= o[1] <= 255: return None
                extra.append(o[1])
            elif k == 'A':
                if o != ('r', 1, 0xe0): return None
            elif k == 'CC':
                if t != 'cc': return None
                field, mask = o[1], 15
            elif k in ('SR', 'F', 'F2'):
                if t != {'SR': 'sr', 'F': 'f', 'F2': 'f2'}[k]: return None
            elif k == 'M8':
                if t != 'm' or o[1] != 'abs' or o[3] not in (0, 8) or o[2] > 0xff: return None
                extra.append(o[2])
            elif k == 'M16':
                if t != 'm' or o[1] != 'abs' or o[2] > 0xffff: return None
                extra += [o[2] & 0xff, o[2] >> 8]
            elif k in ('J16', 'J24'):
                n = int(k[1:]) // 8
                if t != 'i' or o[1] < 0 or o[1] >= 1 << (8 * n) or (n == 2 and o[2]): return None
                extra += [(o[1] >> (8 * i)) & 0xff for i in range(n)]
            elif k in ('D8', 'D16'):
                if t != 'i': return None
                rel = (len(extra), 1 if k == 'D8' else 2, o[1], o[2]); extra += [0] * rel[1]
            else: return None
        if (op & mask) != field: return None
        if mnem in T.BLOCK or mnem in T.COMPARE: return None            # encoded by encode() itself
        out = (pfx or []) + [op] + extra if table is not T.MAIN else [op] + extra
        if rel:
            at, n, target, unknown = rel
            d = ((target - (pc + len(out)) + 0x800000) & 0xffffff) - 0x800000     # addresses wrap at 24 bits
            if not -(1 << (8 * n - 1)) <= d < (1 << (8 * n - 1)):
                if self.final and not unknown: return None
                d = 0
            base = len(out) - len(extra) + at
            for i in range(n): out[base + i] = (d >> (8 * i)) & 0xff
        return out

    def encode(self, mnem, texts, pc):
        suffix = 0
        if mnem not in ALL_MNEMS and mnem[-1] in 'bwl' and mnem[:-1] in T.SUFFIXED:
            suffix, mnem = {'b': 1, 'w': 2, 'l': 4}[mnem[-1]], mnem[:-1]
        if mnem not in ALL_MNEMS: raise AsmError('unknown mnemonic %s' % mnem)
        ops = [self.operand(t, pc, mnem, i, len(texts), suffix) for i, t in enumerate(texts)]
        # forms the tables cannot express
        if mnem == 'di' and not ops: return [0x06, 0x07]
        if mnem == 'ei' and not ops: ops = [('i', 0, False)]
        if mnem == 'ret' and ops and ops[0][0] == 'cc': return [0xb0, 0xf0 + ops[0][1]]
        if mnem == 'ldx':
            if len(ops) != 2 or ops[0][0] != 'm' or ops[1][0] != 'i': raise AsmError('ldx (n),value')
            return [0xf7, 0x00, ops[0][2] & 0xff, 0x00, ops[1][1] & 0xff, 0x00]
        if mnem in T.BLOCK:
            word = 0x10 if suffix == 2 else 0
            pair = 3
            if ops:
                regs = [o[2] for o in ops if o[0] == 'm']
                if regs == [0xf0, 0xf4]: pair = 5
                elif regs != [0xe8, 0xec]: raise AsmError('%s takes (XDE+),(XHL+) or (XIX+),(XIY+)' % mnem)
            return [0x80 + word + pair, 0x10 + T.BLOCK.index(mnem)]
        if mnem in T.COMPARE:
            word = 0x10 if suffix == 2 or (ops and ops[0][:2] == ('r', 2)) else 0
            r = 3
            if len(ops) == 2 and ops[1][0] == 'm' and ops[1][2] in T.CODE16: r = T.CODE16.index(ops[1][2])
            return [0x80 + word + r, 0x14 + T.COMPARE.index(mnem)]
        if mnem == 'ldar':
            if len(ops) == 2 and ops[1][:2] == ('m', 'abs'): ops[1] = ('i', ops[1][2], ops[1][3] == 24)
            if len(ops) != 2 or ops[0][0] != 'r' or ops[0][1] not in (2, 4) or ops[0][2] not in T.CODE16 or ops[1][0] != 'i':
                raise AsmError('ldar R,address')
            d = ((ops[1][1] - (pc + 4) + 0x800000) & 0xffffff) - 0x800000
            if not -32768 <= d <= 32767:
                if self.final and not ops[1][2]: raise AsmError('ldar target out of range')
                d = 0
            return [0xf3, 0x13, d & 0xff, (d >> 8) & 0xff, (0x20 if ops[0][1] == 2 else 0x30) + T.CODE16.index(ops[0][2])]
        # defaults
        if len(ops) == 1 and (mnem in SHIFTS and ops[0][0] == 'r' or mnem in ('inc', 'dec')): ops = [('i', 1, False)] + ops
        if mnem == 'djnz' and len(ops) == 1: ops = [('r', 1, 0xe5)] + ops
        if mnem in ('jp', 'call'):
            if len(ops) == 1 and ops[0][0] == 'm': ops = [('cc', 8)] + ops
            elif len(ops) == 2 and ops[1][0] == 'i': ops = [ops[0], ('m', 'abs', ops[1][1] & 0xffffff, 24 if ops[1][2] else 0)]
        if mnem in ('jr', 'jrl') and len(ops) == 1: ops = [('cc', 8)] + ops
        best = None
        for table in (T.MAIN, T.REG, T.SRC, T.DST):
            for op, entry in table.items():
                if entry[0] != mnem: continue
                b = self.try_entry(table, op, entry, ops, pc, suffix)
                if b is not None and (best is None or len(b) < len(best)): best = b
        if best is None: raise AsmError('no such instruction form: %s %s' % (mnem, ','.join(texts)))
        return best

    # ---- statements
    def emit(self, pc, data):
        for i, b in enumerate(data): self.mem[pc + i] = b & 0xff

    def statement(self, line, pc, where):
        """assemble one source line at pc; returns the new pc"""
        text = line
        q = None
        for i, ch in enumerate(line):                                   # strip the comment
            if q:
                if ch == q: q = None
            elif ch == '"' or (ch == "'" and not line[i - 1:i].isalnum()): q = ch
            elif ch == ';': text = line[:i]; break
        text = text.strip()
        if not text: return pc
        m = re.match(r'^([A-Za-z_.][A-Za-z_0-9.]*)\s*:\s*(.*)$', text)
        if m:
            self.define(m.group(1), pc); text = m.group(2).strip()
            if not text: return pc
        m = re.match(r'^([A-Za-z_.][A-Za-z_0-9.]*)\s+(?:equ|EQU)\s+(.+)$', text) or re.match(r'^([A-Za-z_.][A-Za-z_0-9.]*)\s*=\s*(.+)$', text)
        if m:
            self.define(m.group(1), self.ev(m.group(2), pc)); return pc
        parts = text.split(None, 1)
        mnem, rest = parts[0].lower(), (parts[1] if len(parts) > 1 else '')
        args = split_ops(rest)
        if mnem == 'org':
            pc = self.ev(rest, pc)
            if self.base is None: self.base = pc
            return pc
        if mnem in ('db', 'dw', 'dl'):
            n = {'db': 1, 'dw': 2, 'dl': 4}[mnem]; data = []
            for a in args:
                if n == 1 and len(a) >= 2 and a[0] == a[-1] and a[0] in '"\'': data += [ord(c) for c in a[1:-1]]
                else: v = self.ev(a, pc); data += [(v >> (8 * i)) & 0xff for i in range(n)]
            self.emit(pc, data); return pc + len(data)
        if mnem == 'ds':
            n = self.ev(args[0], pc); fill = self.ev(args[1], pc) if len(args) > 1 else 0
            self.emit(pc, [fill] * n); return pc + n
        if mnem == 'align':
            n = self.ev(rest, pc); pad = (-pc) % n
            self.emit(pc, [0] * pad); return pc + pad
        if mnem == 'incbin':
            data = open(os.path.join(os.path.dirname(where), rest.strip().strip('"')), 'rb').read()
            self.emit(pc, data); return pc + len(data)
        data = self.encode(mnem, args, pc)
        self.emit(pc, data)
        if self.final: self.listing.append('%06x  %-20s %s' % (pc, ' '.join('%02x' % b for b in data), line.rstrip()))
        return pc + len(data)

    def define(self, name, value):
        if name in self.sym and self.sym[name] != value: self.changed = True
        self.sym[name] = value

    def lines(self, path, seen=()):
        for n, line in enumerate(open(path), 1):
            m = re.match(r'^\s*include\s+"([^"]+)"', line)
            if m:
                for x in self.lines(os.path.join(os.path.dirname(path), m.group(1))): yield x
            else: yield (path, n, line.rstrip('\n'))

    def assemble(self, path):
        src = list(self.lines(path))
        for p in range(12):                                             # until no symbol moves
            self.final, self.changed, self.mem, self.listing, self.base = False, False, {}, [], None
            self.sym_pass = dict(self.sym)
            self.run(src)
            if not self.changed and p > 0: break
        else: raise AsmError('labels do not settle')
        self.final, self.mem, self.listing, self.base = True, {}, [], None
        self.sym_pass = dict(self.sym)
        self.run(src)
        lo, hi = self.base, max(self.mem) + 1
        return bytes(self.mem.get(a, 0xff) for a in range(lo, hi))

    def run(self, src):
        pc = 0
        for path, n, line in src:
            try: pc = self.statement(line, pc, path)
            except AsmError as e: raise AsmError('%s:%d: %s\n    %s' % (path, n, e, line.strip()))

def assemble_line(text, pc=0, symbols=None):
    """bytes of one instruction (for tests)"""
    a = Assembler(); a.sym = dict(symbols or {}); a.sym_pass = a.sym; a.final = True
    parts = text.strip().split(None, 1)
    return bytes(a.encode(parts[0].lower(), split_ops(parts[1] if len(parts) > 1 else ''), pc))

def main():
    if len(sys.argv) < 3: print(__doc__); return 2
    a = Assembler()
    try: data = a.assemble(sys.argv[1])
    except AsmError as e: print('asm900: ' + str(e)); return 1
    open(sys.argv[2], 'wb').write(data)
    if '-l' in sys.argv:
        with open(sys.argv[sys.argv.index('-l') + 1], 'w') as f:
            f.write('\n'.join(a.listing) + '\n\n')
            for k in sorted(a.sym, key=lambda k: a.sym[k]): f.write('%06x  %s\n' % (a.sym[k], k))
    print('asm900: %s: %d bytes at 0x%06x' % (sys.argv[2], len(data), a.base))
    return 0

if __name__ == '__main__': sys.exit(main())
