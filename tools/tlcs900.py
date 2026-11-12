"""tlcs900.py - Toshiba TLCS-900/H instruction tables: decoder, formatter and encoder (host only).

The Neo Geo Pocket (Color) CPU is a TMP95C061: a TLCS-900/H core. One set of tables here serves
  tools/dis900.py     the disassembler
  tools/asm900.py     the assembler (for test cartridges; a port never needs it)
  tools/recomp900.py  the static translator

decode(read, pc) -> Insn or None (not an instruction).  read(addr) -> byte.

An Insn has: mnem (lower case, no size suffix), size (0, 1, 2 or 4 bytes: the operation width), ops (a list of
operand tuples), length (bytes) and pc. Operand tuples:
  ('r', size, code)            register, by its full 8-bit register code (see reg_name)
  ('i', value)                 immediate
  ('m', 'ind', code)           (r32)
  ('m', 'disp', code, d)       (r32 + d), d signed
  ('m', 'abs', addr)           (addr)
  ('m', 'idx', code, c2, s2)   (r32 + r8) or (r32 + r16): the index register is signed
  ('m', 'dec', code, step)     (-r32): r32 -= step first
  ('m', 'inc', code, step)     (r32+): r32 += step afterwards
  ('m', 'pcrel', addr)         (pc + d16), already resolved
  ('cc', n)                    condition 0..15 (CC)
  ('a', addr)                  code address: jump, call or branch target
  ('sr',) ('f',) ('f2',)       SR, F and F'
  ('cr', n)                    control register number (LDC)

Checked against MAME's TLCS-900 disassembler tables (all 15 x 256 entries, tools/ngpref/README.md says how).
"""

R8 = ['W', 'A', 'B', 'C', 'D', 'E', 'H', 'L']
R16 = ['WA', 'BC', 'DE', 'HL', 'IX', 'IY', 'IZ', 'SP']
R32 = ['XWA', 'XBC', 'XDE', 'XHL', 'XIX', 'XIY', 'XIZ', 'XSP']
CC = ['F', 'LT', 'LE', 'ULE', 'OV', 'MI', 'Z', 'C', 'T', 'GE', 'GT', 'UGT', 'NOV', 'PL', 'NZ', 'NC']
CC_ALIAS = {'EQ': 6, 'NE': 14, 'ULT': 7, 'UGE': 15, 'PE': 4, 'PO': 12, 'M': 5, 'P': 13}

# the 3-bit register field: its full register code in the current bank
CODE8 = [0xe1, 0xe0, 0xe5, 0xe4, 0xe9, 0xe8, 0xed, 0xec]
CODE16 = [0xe0, 0xe4, 0xe8, 0xec, 0xf0, 0xf4, 0xf8, 0xfc]

_B = ['A', 'W', 'QA', 'QW', 'C', 'B', 'QC', 'QB', 'E', 'D', 'QE', 'QD', 'L', 'H', 'QL', 'QH']
_W = ['WA', 'QWA', 'BC', 'QBC', 'DE', 'QDE', 'HL', 'QHL']
_L = ['XWA', 'XBC', 'XDE', 'XHL']
_BX = ['IXL', 'IXH', 'QIXL', 'QIXH', 'IYL', 'IYH', 'QIYL', 'QIYH', 'IZL', 'IZH', 'QIZL', 'QIZH', 'SPL', 'SPH', 'QSPL', 'QSPH']
_WX = ['IX', 'QIX', 'IY', 'QIY', 'IZ', 'QIZ', 'SP', 'QSP']
_LX = ['XIX', 'XIY', 'XIZ', 'XSP']

def reg_valid(code):
    return code < 0x40 or code >= 0xd0

def reg_name(size, code):
    """name of the register with this full code, e.g. (1, 0xe0) -> 'A', (2, 0x34) -> 'RBC3', (4, 0xf0) -> 'XIX'"""
    if not reg_valid(code): return '?%02x' % code
    hi, lo = code >> 4, code & 15
    if size == 1: n = _BX[lo] if hi == 15 else _B[lo]
    elif size == 2: n = _WX[lo >> 1] if hi == 15 else _W[lo >> 1]
    else: n = _LX[lo >> 2] if hi == 15 else _L[lo >> 2]
    if hi < 4: return ('R' if size < 4 and not n.startswith('Q') else '') + n + str(hi)
    if hi == 13: return n + "'"
    return n

def _reg_table():
    t = {}
    for size in (1, 2, 4):
        step = size
        for code in list(range(0, 0x40, step)) + list(range(0xd0, 0x100, step)):
            t[reg_name(size, code).upper()] = (size, code)
    return t
REG_BY_NAME = _reg_table()

class Insn(object):
    __slots__ = ('mnem', 'size', 'ops', 'length', 'pc', 'spec')
    def __init__(self, mnem, size, ops, length, pc, spec):
        self.mnem, self.size, self.ops, self.length, self.pc, self.spec = mnem, size, ops, length, pc, spec
    def __repr__(self): return 'Insn(%s)' % fmt(self)

# ---------------------------------------------------------------------------------------------------- tables
# Operand kinds in the tables:
#   first-byte table:  R8 R16 R32 (register in bits 0-2)  I8 I16 I32 (immediate)  M8 ((n8))  J16 J24 (jump address)
#                      D8 D16 (pc-relative)  CC (bits 0-3)  Q3 (bits 0-2 as a number)  SR A F F2
#   after a prefix:    r (the prefix register)  M (the prefix memory operand)  R (register of the operation size in
#                      bits 0-2)  R8 R16 R32  RR (double-size register in bits 0-2, MUL/DIV)  rr (double-size
#                      register holding r)  I (immediate of the operation size)  I8 I16  N3 (bits 0-2, 0 means 8)
#                      Q3 (bits 0-2)  N4 (next byte & 15)  A  CC  D8  CR (next byte)  M16 ((n16))
def _expand(text):
    t = {}
    for item in text.replace('\n', '|').split('|'):
        item = item.strip()
        if not item: continue
        p = item.split()
        rng, sizes, mnem = p[0], p[1], p[2]
        ops = p[3].split(',') if len(p) > 3 else []
        lo, hi = (rng.split('-') + [rng])[:2]
        for op in range(int(lo, 16), int(hi, 16) + 1): t[op] = (mnem, sizes, ops)
    return t

# sizes column: letters of the operation sizes the entry exists for (b, w, l); '-' = no size; a digit = fixed size
MAIN = _expand("""
00 - nop|02 2 push SR|03 2 pop SR|05 - halt|06 - ei I8|07 - reti|08 1 ld M8,I8|09 1 push I8|0a 2 ld M8,I16|0b 2 push I16
0c - incf|0d - decf|0e - ret|0f - retd I16|10 - rcf|11 - scf|12 - ccf|13 - zcf|14 1 push A|15 1 pop A|16 - ex F,F2
17 - ldf I8|18 1 push F|19 1 pop F|1a - jp J16|1b - jp J24|1c - call J16|1d - call J24|1e - calr D16
20-27 1 ld R8,I8|28-2f 2 push R16|30-37 2 ld R16,I16|38-3f 4 push R32|40-47 4 ld R32,I32|48-4f 2 pop R16|58-5f 4 pop R32
60-6f - jr CC,D8|70-7f - jrl CC,D16|f7 - ldx|f8-ff - swi Q3
""")
REG = _expand("""
03 bwl ld r,I|04 bwl push r|05 bwl pop r|06 bw cpl r|07 bw neg r|08 bw mul rr,I|09 bw muls rr,I|0a bw div rr,I|0b bw divs rr,I
0c l link r,I16|0d l unlk r|0e w bs1f A,r|0f w bs1b A,r|10 b daa r|12 wl extz r|13 wl exts r|14 wl paa r|16 w mirr r|19 w mula r
1c bw djnz r,D8|20 bw andcf N4,r|21 bw orcf N4,r|22 bw xorcf N4,r|23 bw ldcf N4,r|24 bw stcf N4,r
28 bw andcf A,r|29 bw orcf A,r|2a bw xorcf A,r|2b bw ldcf A,r|2c bw stcf A,r|2e bwl ldc CR,r|2f bwl ldc r,CR
30 bw res N4,r|31 bw set N4,r|32 bw chg N4,r|33 bw bit N4,r|34 bw tset N4,r
38 w minc1 I16,r|39 w minc2 I16,r|3a w minc4 I16,r|3c w mdec1 I16,r|3d w mdec2 I16,r|3e w mdec4 I16,r
40-47 bw mul RR,r|48-4f bw muls RR,r|50-57 bw div RR,r|58-5f bw divs RR,r|60-67 bwl inc N3,r|68-6f bwl dec N3,r
70-7f bw scc CC,r|80-87 bwl add R,r|88-8f bwl ld R,r|90-97 bwl adc R,r|98-9f bwl ld r,R|a0-a7 bwl sub R,r|a8-af bwl ld r,Q3
b0-b7 bwl sbc R,r|b8-bf bw ex R,r|c0-c7 bwl and R,r|c8 bwl add r,I|c9 bwl adc r,I|ca bwl sub r,I|cb bwl sbc r,I
cc bwl and r,I|cd bwl xor r,I|ce bwl or r,I|cf bwl cp r,I|d0-d7 bwl xor R,r|d8-df bwl cp r,Q3|e0-e7 bwl or R,r
e8 bwl rlc N4,r|e9 bwl rrc N4,r|ea bwl rl N4,r|eb bwl rr N4,r|ec bwl sla N4,r|ed bwl sra N4,r|ee bwl sll N4,r|ef bwl srl N4,r
f0-f7 bwl cp R,r|f8 bwl rlc A,r|f9 bwl rrc A,r|fa bwl rl A,r|fb bwl rr A,r|fc bwl sla A,r|fd bwl sra A,r|fe bwl sll A,r|ff bwl srl A,r
""")
SRC = _expand("""
04 bw push M|06 b rld A,M|07 b rrd A,M|10 bw ldi|11 bw ldir|12 bw ldd|13 bw lddr|14 bw cpi|15 bw cpir|16 bw cpd|17 bw cpdr
19 bw ld M16,M|20-27 bwl ld R,M|30-37 bw ex M,R|38 bw add M,I|39 bw adc M,I|3a bw sub M,I|3b bw sbc M,I
3c bw and M,I|3d bw xor M,I|3e bw or M,I|3f bw cp M,I|40-47 bw mul RR,M|48-4f bw muls RR,M|50-57 bw div RR,M|58-5f bw divs RR,M
60-67 bw inc N3,M|68-6f bw dec N3,M|78 bw rlc M|79 bw rrc M|7a bw rl M|7b bw rr M|7c bw sla M|7d bw sra M|7e bw sll M|7f bw srl M
80-87 bwl add R,M|88-8f bwl add M,R|90-97 bwl adc R,M|98-9f bwl adc M,R|a0-a7 bwl sub R,M|a8-af bwl sub M,R
b0-b7 bwl sbc R,M|b8-bf bwl sbc M,R|c0-c7 bwl and R,M|c8-cf bwl and M,R|d0-d7 bwl xor R,M|d8-df bwl xor M,R
e0-e7 bwl or R,M|e8-ef bwl or M,R|f0-f7 bwl cp R,M|f8-ff bwl cp M,R
""")
DST = _expand("""
00 1 ld M,I8|02 2 ld M,I16|04 1 pop M|06 2 pop M|14 1 ld M,M16|16 2 ld M,M16|20-27 2 lda R16,M|28 1 andcf A,M|29 1 orcf A,M
2a 1 xorcf A,M|2b 1 ldcf A,M|2c 1 stcf A,M|30-37 4 lda R32,M|40-47 1 ld M,R8|50-57 2 ld M,R16|60-67 4 ld M,R32
80-87 1 andcf Q3,M|88-8f 1 orcf Q3,M|90-97 1 xorcf Q3,M|98-9f 1 ldcf Q3,M|a0-a7 1 stcf Q3,M|a8-af 1 tset Q3,M
b0-b7 1 res Q3,M|b8-bf 1 set Q3,M|c0-c7 1 chg Q3,M|c8-cf 1 bit Q3,M|d0-df - jp CC,M|e0-ef - call CC,M|f0-ff - ret CC
""")
BLOCK = ('ldi', 'ldir', 'ldd', 'lddr')              # only with the prefixes (XHL) and (XIY): bytes x3 and x5
COMPARE = ('cpi', 'cpir', 'cpd', 'cpdr')            # only with a plain (r32) prefix
# mnemonics that take a size suffix when no register operand shows the width (word forms end in 'w')
SUFFIXED = ('ld', 'push', 'pop', 'inc', 'dec', 'add', 'adc', 'sub', 'sbc', 'and', 'xor', 'or', 'cp', 'rlc', 'rrc', 'rl',
            'rr', 'sla', 'sra', 'sll', 'srl', 'ldi', 'ldir', 'ldd', 'lddr', 'cpi', 'cpir', 'cpd', 'cpdr')
SIZE_OF = {'b': 1, 'w': 2, 'l': 4}

def _s8(v): return v - 256 if v >= 128 else v
def _s16(v): return v - 65536 if v >= 32768 else v

# ---------------------------------------------------------------------------------------------------- decoder
def decode(read, pc):
    """Decode the instruction at pc. Returns an Insn, or None when the bytes are not a TLCS-900/H instruction."""
    p = [pc]
    def u8():
        v = read(p[0]) & 0xff; p[0] += 1; return v
    def u16(): lo = u8(); return lo | u8() << 8
    def u24(): lo = u16(); return lo | u8() << 16
    def u32(): lo = u16(); return lo | u16() << 16
    def imm(size): return u8() if size == 1 else u16() if size == 2 else u32()

    first = u8()
    grp = first & 0xf0
    if first < 0x80 or first >= 0xf7:                       # one-byte opcodes (and LDX, SWI)
        e = MAIN.get(first)
        if not e: return None
        mnem, sizes, kinds = e
        size = int(sizes) if sizes.isdigit() else 0
        ops = []
        for k in kinds:
            if k == 'R8': ops.append(('r', 1, CODE8[first & 7]))
            elif k == 'R16': ops.append(('r', 2, CODE16[first & 7]))
            elif k == 'R32': ops.append(('r', 4, CODE16[first & 7]))
            elif k == 'I8': ops.append(('i', u8()))
            elif k == 'I16': ops.append(('i', u16()))
            elif k == 'I32': ops.append(('i', u32()))
            elif k == 'M8': ops.append(('m', 'abs', u8()))
            elif k == 'J16': ops.append(('a', u16()))
            elif k == 'J24': ops.append(('a', u24()))
            elif k == 'D8': d = _s8(u8()); ops.append(('a', (p[0] + d) & 0xffffff))
            elif k == 'D16': d = _s16(u16()); ops.append(('a', (p[0] + d) & 0xffffff))
            elif k == 'CC': ops.append(('cc', first & 15))
            elif k == 'Q3': ops.append(('i', first & 7))
            elif k == 'SR': ops.append(('sr',))
            elif k == 'A': ops.append(('r', 1, 0xe0))
            elif k == 'F': ops.append(('f',))
            elif k == 'F2': ops.append(('f2',))
        if mnem == 'ldx':                                   # F7 00 n 00 v 00
            if u8() != 0: return None
            a = u8()
            if u8() != 0: return None
            v = u8()
            if u8() != 0: return None
            ops = [('m', 'abs', a), ('i', v)]; size = 1
        return Insn(mnem, size, ops, p[0] - pc, pc, e)

    low = first & 15
    zz = (first >> 4) & 3                                   # 0 byte, 1 word, 2 long, 3 destination
    prefix = None
    if grp < 0xc0:                                          # 8x 9x Ax Bx: (r32) and (r32 + d8)
        code = CODE16[first & 7]
        prefix = ('m', 'ind', code) if low < 8 else ('m', 'disp', code, _s8(u8()))
        is_reg = False
    elif low < 6:                                           # Cx Dx Ex Fx 0-5: the other memory modes
        is_reg = False
        if low == 0: prefix = ('m', 'abs', u8())
        elif low == 1: prefix = ('m', 'abs', u16())
        elif low == 2: prefix = ('m', 'abs', u24())
        elif low == 3:
            b = u8()
            if b & 3 == 0: prefix = ('m', 'ind', b)
            elif b & 3 == 1: prefix = ('m', 'disp', b & 0xfc, _s16(u16()))
            elif b == 0x03: c = u8(); prefix = ('m', 'idx', c, u8(), 1)
            elif b == 0x07: c = u8(); prefix = ('m', 'idx', c, u8(), 2)
            elif b == 0x13: d = _s16(u16()); prefix = ('m', 'pcrel', (p[0] + d) & 0xffffff)
            else: return None
        else:
            b = u8()
            if b & 3 == 3: return None
            prefix = ('m', 'dec' if low == 4 else 'inc', b & 0xfc, 1 << (b & 3))
        if prefix[1] in ('ind', 'disp', 'idx', 'dec', 'inc') and not reg_valid(prefix[2]): return None
        if prefix[1] == 'idx':                              # register codes are used without their low bits
            if not reg_valid(prefix[3]): return None
            prefix = ('m', 'idx', prefix[2] & 0xfc, prefix[3] & (0xff if prefix[4] == 1 else 0xfe), prefix[4])
    elif low == 6 or zz == 3: return None                   # C6 D6 E6 F6, and F7-FF were handled above
    else:                                                   # C7-CF D7-DF E7-EF: a register
        is_reg = True
        size = 1 << zz
        if low == 7:
            code = u8()
            if not reg_valid(code): return None
            code &= 0x100 - size                            # a word or long register ignores the low bits of its code
        else: code = (CODE8 if size == 1 else CODE16)[first & 7]
        prefix = ('r', size, code)

    second = u8()
    if is_reg: table, size = REG, prefix[1]
    elif zz == 3: table, size = DST, 0
    else: table, size = SRC, 1 << zz
    e = table.get(second)
    if not e: return None
    mnem, sizes, kinds = e
    if table is DST:
        size = int(sizes) if sizes.isdigit() else 0
        if mnem == 'ret' and first != 0xb0: return None
    elif 'bwl'[zz] not in sizes: return None
    if mnem in BLOCK and first not in (0x83, 0x85, 0x93, 0x95): return None
    if mnem in COMPARE and not (first & 0xe8) == 0x80: return None
    ops = []
    for k in kinds:
        if k in ('r', 'M'): ops.append(prefix)
        elif k == 'R': ops.append(('r', size, (CODE8 if size == 1 else CODE16)[second & 7]))
        elif k == 'R8': ops.append(('r', 1, CODE8[second & 7]))
        elif k == 'R16': ops.append(('r', 2, CODE16[second & 7]))
        elif k == 'R32': ops.append(('r', 4, CODE16[second & 7]))
        elif k == 'RR':                                     # MUL/DIV destination: twice the operation size
            if size == 1:
                if not second & 1: return None              # only WA, BC, DE, HL (odd numbers)
                ops.append(('r', 2, CODE16[(second & 7) >> 1]))
            else: ops.append(('r', 4, CODE16[second & 7]))
        elif k == 'rr':                                     # MUL/DIV rr,#: the double-size register that holds r
            c = prefix[2]
            if size == 1:
                if low != 7 and not first & 1: return None
                ops.append(('r', 2, c & 0xfe))
            else: ops.append(('r', 4, c & 0xfc))
        elif k == 'I': ops.append(('i', imm(size)))
        elif k == 'I8': ops.append(('i', u8()))
        elif k == 'I16': ops.append(('i', u16()))
        elif k == 'N3': ops.append(('i', (second & 7) or 8))
        elif k == 'Q3': ops.append(('i', second & 7))
        elif k == 'N4': ops.append(('i', u8() & 15))
        elif k == 'A': ops.append(('r', 1, 0xe0))
        elif k == 'CC': ops.append(('cc', second & 15))
        elif k == 'D8': d = _s8(u8()); ops.append(('a', (p[0] + d) & 0xffffff))
        elif k == 'CR': ops.append(('cr', u8()))
        elif k == 'M16': ops.append(('m', 'abs', u16()))
    if mnem == 'link': ops[1] = ('i', _s16(ops[1][1]))
    if mnem in ('jp', 'call') and prefix[1] == 'abs': ops[1] = ('a', prefix[2])    # a static target
    if mnem in COMPARE: ops = [('r', size, 0xe0), ('m', 'inc' if mnem in ('cpi', 'cpir') else 'dec', CODE16[first & 7], size)]
    if mnem in BLOCK:
        d, s = (0xe8, 0xec) if first & 7 == 3 else (0xf0, 0xf4)        # (XDE),(XHL) or (XIX),(XIY)
        k = 'inc' if mnem in ('ldi', 'ldir') else 'dec'
        ops = [('m', k, d, size), ('m', k, s, size)]
    return Insn(mnem, size, ops, p[0] - pc, pc, e)

# ---------------------------------------------------------------------------------------------------- formatter
def fmt_op(op, size):
    k = op[0]
    if k == 'r': return reg_name(op[1], op[2])
    if k == 'i': return ('-0x%x' % -op[1]) if op[1] < 0 else ('0x%x' % op[1]) if op[1] > 9 else '%d' % op[1]
    if k == 'a': return '0x%06x' % op[1]
    if k == 'cc': return CC[op[1]]
    if k == 'sr': return 'SR'
    if k == 'f': return 'F'
    if k == 'f2': return "F'"
    if k == 'cr': return 'CR%02x' % op[1]
    m = op[1]
    if m == 'ind': return '(%s)' % reg_name(4, op[2])
    if m == 'disp': return '(%s%s0x%x)' % (reg_name(4, op[2]), '-' if op[3] < 0 else '+', abs(op[3]))
    if m == 'abs': return '(0x%x)' % op[2]
    if m == 'idx': return '(%s+%s)' % (reg_name(4, op[2]), reg_name(op[4], op[3]))
    if m == 'dec': return '(-%s:%d)' % (reg_name(4, op[2]), op[3])
    if m == 'inc': return '(%s+:%d)' % (reg_name(4, op[2]), op[3])
    if m == 'pcrel': return '(0x%06x)' % op[2]
    return '?'

def fmt(ins):
    """text of an instruction, in the syntax tools/asm900.py reads back"""
    m, ops = ins.mnem, ins.ops
    if m == 'ei' and ops[0][1] == 7: return 'di'
    if m in ('jp', 'call', 'jr', 'jrl', 'ret', 'scc') and ops and ops[0][0] == 'cc' and ops[0][1] == 8 and m != 'scc':
        ops = ops[1:]                                       # the condition T is not written
    name = m
    if m in SUFFIXED and not any(o[0] == 'r' for o in ops) and ins.size in (2, 4) and not (m == 'push' and ops and ops[0][0] in ('sr',)):
        name = m + ('w' if ins.size == 2 else 'l')
    if m in ('pop', 'push') and ops and ops[0][0] in ('sr', 'f'): name = m
    if m == 'lda' and ops[1][1] == 'pcrel': name = 'ldar'
    return (name + ' ' + ','.join(fmt_op(o, ins.size) for o in ops)).strip()

# flow classification, for the tracer and the translator
def flow(ins):
    """('jump'|'call'|'branch'|'ret'|'swi'|'halt'|None, condition or None, static target or None)"""
    m, ops = ins.mnem, ins.ops
    if m in ('jr', 'jrl'):
        return ('jump' if ops[0][1] == 8 else 'branch', None if ops[0][1] == 8 else ops[0][1], ops[1][1])
    if m == 'djnz': return ('branch', 'djnz', ops[1][1])
    if m in ('jp', 'call'):
        kind = 'jump' if m == 'jp' else 'call'
        if len(ops) == 1: return (kind, None, ops[0][1])
        cc = None if ops[0][1] == 8 else ops[0][1]
        return (kind if cc is None else ('branch' if m == 'jp' else 'call'), cc, ops[1][1] if ops[1][0] == 'a' else None)
    if m == 'calr': return ('call', None, ops[0][1])
    if m in ('ret', 'retd', 'reti'):
        cc = ops[0][1] if (m == 'ret' and ops and ops[0][1] != 8) else None
        return ('ret', cc, None)
    if m == 'swi': return ('swi', None, None)
    if m == 'halt': return ('halt', None, None)
    return (None, None, None)
