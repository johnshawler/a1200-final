#!/usr/bin/env python3
"""dis900.py file start end [-b base] - disassemble TLCS-900/H code (addresses in hex).

file is a cartridge ROM (mapped at 0x200000, the second 2 MB at 0x800000) or, with -b ff0000, a BIOS image.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tlcs900 import decode, fmt

def main():
    if len(sys.argv) < 4: print(__doc__); return 2
    data = open(sys.argv[1], 'rb').read()
    base = int(sys.argv[sys.argv.index('-b') + 1], 16) if '-b' in sys.argv else 0x200000
    a, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
    def rd(x):
        x &= 0xffffff
        if base == 0x200000 and x >= 0x800000: x -= 0x600000
        return data[x - base] if 0 <= x - base < len(data) else 0xff
    while a < end:
        d = decode(rd, a)
        if not d: print('%06x: %-24s db 0x%02x' % (a, '%02x' % rd(a), rd(a))); a += 1; continue
        print('%06x: %-24s %s' % (a, ' '.join('%02x' % rd(a + i) for i in range(d.length)), fmt(d))); a += d.length
    return 0

if __name__ == '__main__': sys.exit(main())
