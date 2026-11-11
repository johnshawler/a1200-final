#!/usr/bin/env python3
"""gfx.py - turn byte-per-pixel game graphics into Amiga data. Run on the host at build time.

Pixels are lists (or bytes) of palette indices, row by row. All output is big-endian binary, ready for
incbin into a chip-RAM section. Functions:

  compact(images, palette, keep0=True)  -> (images, palette, mapping): renumber the colours actually used to 0..n-1
  planes_needed(ncolours)               -> bitplanes for that many colours
  tile(pixels16x16, planes)             -> planes x 16 words (plane 0 first)
  bob(pixels, w, h, planes)             -> (image, mask, wwords): image = planes x h x wwords words, mask = h x wwords words
  picture(pixels, w, h, planes)         -> planes x h x (w/8) bytes: a whole background, w multiple of 16
  sprite(pixels, w, h, width=16)        -> one 3-colour sprite block (pixel values 0..3)
  sprite_pair(pixels, w, h, width=16)   -> (even, odd): a 15-colour attached pair (pixel values 0..15)
  palette(rgb_triples)                  -> 4 bytes per colour: 0x00RRGGBB
  sound(values, rate)                   -> (data, period, length_words): a sample for ptplayer / Paula
  read_ppm(path)                        -> (w, h, rgb_triples): load a binary PPM (P6) picture
  index_image(rgb_triples)              -> (pixels, palette): give every distinct colour an index (first seen = 0)
"""
import struct

def compact(images, pal, keep0=True):
    used = sorted({p for img in images for p in img})
    if keep0 and 0 in used: used.remove(0)
    order = ([0] if keep0 else []) + used
    mapping = {old: new for new, old in enumerate(order)}
    return [bytes(mapping[p] for p in img) for img in images], [pal[o] for o in order], mapping

def planes_needed(ncolours):
    n = 1
    while (1 << n) < ncolours: n += 1
    return n

def _rows(pixels, w, h, wbytes, plane):
    out = bytearray(wbytes * h)
    for y in range(h):
        for x in range(w):
            if (pixels[y * w + x] >> plane) & 1:
                out[y * wbytes + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)

def tile(pixels, planes):
    assert len(pixels) == 256
    return b''.join(_rows(pixels, 16, 16, 2, p) for p in range(planes))

def bob(pixels, w, h, planes, transparent=0):
    wwords = (w + 15) // 16
    image = b''.join(_rows(pixels, w, h, wwords * 2, p) for p in range(planes))
    mask = _rows([0 if p == transparent else 1 for p in pixels], w, h, wwords * 2, 0)
    return image, mask, wwords

def picture(pixels, w, h, planes):
    assert w % 16 == 0
    return b''.join(_rows(pixels, w, h, w // 8, p) for p in range(planes))

def _sprite_block(pixels, w, h, width, lo, hi):
    slot = width // 8                                   # bytes per plane per line: 2, 4 or 8
    out = bytearray(slot * 2)                           # two control slots, filled in by disp_sprite()
    for y in range(h):
        row = [pixels[y * w + x] if x < w else 0 for x in range(width)]
        for bit in (lo, hi):
            b = bytearray(slot)
            for x, p in enumerate(row):
                if (p >> bit) & 1: b[x >> 3] |= 0x80 >> (x & 7)
            out += b
    return bytes(out + bytes(slot * 2))                 # end marker

def sprite(pixels, w, h, width=16):
    assert max(pixels) <= 3 and w <= width
    return _sprite_block(pixels, w, h, width, 0, 1)

def sprite_pair(pixels, w, h, width=16):
    assert max(pixels) <= 15 and w <= width
    return _sprite_block(pixels, w, h, width, 0, 1), _sprite_block(pixels, w, h, width, 2, 3)

def palette(rgb):
    return b''.join(struct.pack('>I', (r << 16) | (g << 8) | b) for r, g, b in rgb)

def sound(values, rate, signed=True):
    """values: sample points, -128..127 (signed=True) or 0..255 (signed=False). rate: sample rate in Hz.
    Returns (data, period, length_words). data starts with two zero bytes and has an even length, as
    ptplayer needs; period = 3546895 / rate (PAL), not below 124."""
    body = bytes((v if signed else v - 128) & 0xff for v in values)
    data = b'\0\0' + body
    if len(data) & 1: data += b'\0'
    return data, max(124, round(3546895 / rate)), len(data) // 2

def read_ppm(path):
    raw = open(path, 'rb').read()
    tok, pos = [], 0
    while len(tok) < 4:                                  # magic, width, height, maxval; '#' starts a comment
        while raw[pos:pos + 1].isspace(): pos += 1
        if raw[pos:pos + 1] == b'#':
            while raw[pos:pos + 1] != b'\n': pos += 1
            continue
        end = pos
        while not raw[end:end + 1].isspace(): end += 1
        tok.append(raw[pos:end]); pos = end
    assert tok[0] == b'P6' and tok[3] == b'255', 'need a binary 8-bit PPM (P6)'
    w, h = int(tok[1]), int(tok[2]); pos += 1
    return w, h, [tuple(raw[pos + i * 3:pos + i * 3 + 3]) for i in range(w * h)]

def index_image(rgb):
    pal, seen, out = [], {}, bytearray()
    for c in rgb:
        if c not in seen: seen[c] = len(pal); pal.append(c)
        out.append(seen[c])
    assert len(pal) <= 256, 'more than 256 colours: reduce the image first'
    return bytes(out), pal
