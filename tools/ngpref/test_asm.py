#!/usr/bin/env python3
"""test_asm.py [file ...] - round trip between the decoder and the assembler of tools/tlcs900.py / asm900.py.

Every byte sequence that decodes (taken from random bytes and, when files are given, from every offset of those
files) is printed, assembled again from that text and decoded once more: the two texts must be equal, and when
the assembler produced the same number of bytes, the bytes must be equal too. No ROM is needed: random bytes
alone reach every table entry.
"""
import sys, os, random
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import tlcs900 as T, asm900 as A

def check(data, pc0, offsets, stats):
    rd = lambda a: data[(a - pc0) % len(data)]
    for off in offsets:
        pc = pc0 + off
        d = T.decode(rd, pc)
        stats['tried'] += 1
        if not d: continue
        if off + d.length > len(data): continue
        text = T.fmt(d)
        stats['decoded'] += 1
        stats['forms'].add((d.mnem, d.size, tuple(o[:2] if o[0] == 'm' else o[0] for o in d.ops)))
        try: b = A.assemble_line(text, pc)
        except A.AsmError as e:
            stats['bad'] += 1
            if stats['bad'] <= 15: print('ASM FAILS  %06x %-22s %s   (%s)' % (pc, data[off:off + d.length].hex(), text, e))
            continue
        d2 = T.decode(lambda a: b[a - pc] if 0 <= a - pc < len(b) else 0, pc)
        t2 = T.fmt(d2) if d2 else None
        orig = bytes(data[off:off + d.length])
        if t2 != text or (len(b) == d.length and b != orig and not same_meaning(d, d2)):
            stats['bad'] += 1
            if stats['bad'] <= 15: print('MISMATCH   %06x %-22s %-28s -> %s %s' % (pc, orig.hex(), text, b.hex(), t2))

def norm(d):
    ops = [('m', 'abs', o[2]) if o[:2] == ('m', 'pcrel') else o for o in d.ops]         # (pc+d16) names an address
    if d.mnem in ('jp', 'call') and ops[0] == ('cc', 8): ops = ops[1:]                   # JP T,addr is JP addr
    if d.mnem in ('mul', 'muls', 'div', 'divs') and ops[0][0] == 'r': ops[0] = ops[0][:2] + (ops[0][2] & 0xfe,)
    return (d.mnem, d.size, ops)

def same_meaning(d, d2):
    return d2 is not None and norm(d) == norm(d2)

def main():
    stats = {'tried': 0, 'decoded': 0, 'bad': 0, 'forms': set()}
    rnd = random.Random(1)
    blob = bytes(rnd.randrange(256) for _ in range(1 << 18))
    check(blob, 0x200000, range(len(blob) - 16), stats)
    for name in sys.argv[1:]:
        data = open(name, 'rb').read()
        base = 0xff0000 if len(data) == 0x10000 else 0x200000
        offs = range(len(data) - 16) if len(data) <= 0x40000 else sorted(rnd.sample(range(len(data) - 16), 400000))
        check(data, base, offs, stats)
    print('%d offsets tried, %d decoded, %d distinct instruction forms, %d failures' % (stats['tried'], stats['decoded'], len(stats['forms']), stats['bad']))
    return 1 if stats['bad'] else 0

sys.exit(main())
