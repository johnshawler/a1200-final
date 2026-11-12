#!/usr/bin/env python3
"""check_tables.py <path to MAME's src/devices/cpu/tlcs900/dasm900.cpp>

Compares the opcode tables of tools/tlcs900.py with the tables of MAME's TLCS-900 disassembler (BSD-3-Clause,
Wilbert Pol), entry by entry: all 15 tables of 256. MAME's file is read as text; nothing of it is run or copied.
Get it from any MAME source archive. Prints the differences; the known, intended ones are listed in KNOWN.
"""
import re, sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import tlcs900 as T

def load(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    tabs = {}
    for m in re.finditer(r'tlcs900_disassembler::(mnemonic(?:_[0-9a-f]{2})?)\[256\]\s*=\s*\{(.*?)\n\};', src, re.S):
        ents = re.findall(r'\{\s*(?:M_)?(\w+)\s*,\s*O_(\w+)\s*,\s*O_(\w+)\s*\}', m.group(2))
        assert len(ents) == 256, (m.group(1), len(ents))
        tabs[m.group(1)] = [(a.lower(), [x for x in (b, c) if x != 'NONE']) for a, b, c in ents]
    return tabs

WORD_NAMES = {b + 'w': b for b in T.SUFFIXED}
SZ = {1: '8', 2: '16', 4: '32'}
def mine(entry, size, table):
    """my table entry in MAME's vocabulary"""
    if not entry: return ('db', [])
    mnem, sizes, kinds = entry
    if table in ('reg', 'src') and 'bwl'[{1: 0, 2: 1, 4: 2}[size]] not in sizes: return ('db', [])
    if table in ('dst', 'main'): size = int(sizes) if sizes.isdigit() else 0
    out = []
    for k in kinds:
        if k == 'r': out.append('R')
        elif k == 'rr': out.append('R')
        elif k == 'R': out.append('C' + SZ[size])
        elif k in ('R8', 'R16', 'R32'): out.append('C' + k[1:])
        elif k == 'RR': out.append('MC16' if size == 1 else 'C32')
        elif k == 'I': out.append('I' + SZ[size])
        elif k in ('N3', 'Q3'): out.append('I3')
        elif k == 'N4': out.append('I8')
        elif k == 'CR': out.append('CR' + SZ[size])
        elif k in ('J16', 'J24'): out.append('I' + k[1:])
        elif k == 'F2': out.append('F')
        else: out.append(k)                                 # M A CC D8 D16 I8 I16 I32 M8 M16 SR F
    return (mnem, out)

KNOWN = {('mnemonic', 0x01): 'NORMAL exists on the TLCS-900 only, not on the 900/H',
         ('mnemonic', 0x04): 'MAX exists on the TLCS-900 only, not on the 900/H'}

def main():
    tabs = load(sys.argv[1])
    plan = [('mnemonic_c8', T.REG, 1, 'reg'), ('mnemonic_d8', T.REG, 2, 'reg'), ('mnemonic_e8', T.REG, 4, 'reg'),
            ('mnemonic_80', T.SRC, 1, 'src'), ('mnemonic_90', T.SRC, 2, 'src'), ('mnemonic_a0', T.SRC, 4, 'src'),
            ('mnemonic_88', T.SRC, 1, 'src2'), ('mnemonic_98', T.SRC, 2, 'src2'), ('mnemonic_c0', T.SRC, 1, 'src2'),
            ('mnemonic_d0', T.SRC, 2, 'src2'), ('mnemonic_e0', T.SRC, 4, 'src2'),
            ('mnemonic_b0', T.DST, 0, 'dst'), ('mnemonic_b8', T.DST, 0, 'dst2'), ('mnemonic_f0', T.DST, 0, 'dst2'),
            ('mnemonic', T.MAIN, 0, 'main')]
    bad = total = 0
    for name, table, size, kind in plan:
        for op in range(256):
            theirs = tabs[name][op]
            tm, to = theirs
            if kind == 'main' and tm in ('80', '88', '90', '98', 'a0', 'a8', 'b0', 'b8', 'c0', 'oc8', 'd0', 'od8', 'e0', 'e8', 'f0'):
                continue                                    # prefix bytes: decoded by code, compared through the other tables
            if tm in WORD_NAMES and (size == 2 or kind in ('dst', 'dst2', 'main')): tm = WORD_NAMES[tm]
            e = table.get(op)
            if kind == 'src2' and e and (e[0] in T.BLOCK or e[0] in T.COMPARE): e = None    # only with plain (r32) prefixes
            if kind == 'dst2' and e and e[0] == 'ret': e = None                             # RET cc is B0 F0+cc only
            if kind == 'main' and e and e[0] == 'ldx': e = ('ldx', '-', [])
            m = mine(e, size, kind.rstrip('2'))
            if kind == 'main' and op == 0x16: to = ['F', 'F']
            total += 1
            if m != (tm, to):
                if (name, op) in KNOWN: continue
                bad += 1
                print('%-12s %02x  tlcs900.py: %-22s MAME: %s' % (name, op, m[0] + ' ' + ','.join(m[1]), tm + ' ' + ','.join(to)))
    print('%d table entries compared, %d differ (%d known, intended differences not counted)' % (total, bad, len(KNOWN)))
    return 1 if bad else 0

sys.exit(main())
