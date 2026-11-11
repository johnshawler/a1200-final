#!/usr/bin/env python3
"""dis.py rom bank start end - disassemble one PRG bank region (bank 7 = fixed $C000)."""
import sys; sys.path.insert(0, 'tools'); from op6502 import decode, fmt
rom = open(sys.argv[1], 'rb').read(); prg = rom[16:16 + rom[4] * 16384]
bank, a, end = int(sys.argv[2]), int(sys.argv[3], 16), int(sys.argv[4], 16)
rd = lambda x: prg[bank * 0x4000 + (x & 0x3fff)]
while a < end:
    d = decode(rd, a)
    if not d: print('%04x: .db $%02x' % (a, rd(a))); a += 1; continue
    m, mode, arg, n = d
    print('%04x: %-9s %s' % (a, ' '.join('%02x' % rd(a + i) for i in range(n)), fmt(m, mode, arg))); a += n
