#!/usr/bin/env python3
"""Generate the scroller's data files (all multi-byte values little-endian, as the PC game expects)."""
import math, random, struct
random.seed(1200)

# ---- palette: 256 x RGB -------------------------------------------------------------------
pal = [(0, 0, 0)] * 256
def ramp(first, n, c0, c1):
    for i in range(n):
        t = i / (n - 1)
        pal[first + i] = tuple(int(c0[k] + (c1[k] - c0[k]) * t) for k in range(3))
ramp(1, 15, (40, 22, 10), (220, 170, 110))      # earth
ramp(16, 16, (20, 24, 40), (170, 180, 210))     # rock
ramp(32, 16, (8, 40, 12), (120, 230, 90))       # plants
ramp(48, 16, (30, 30, 34), (235, 235, 245))     # metal
ramp(64, 16, (10, 20, 80), (200, 230, 255))     # player
ramp(80, 16, (60, 0, 30), (255, 120, 200))      # enemies
ramp(96, 16, (120, 10, 0), (255, 250, 160))     # explosion
pal[112], pal[113], pal[114] = (90, 90, 110), (160, 160, 180), (255, 255, 255)
pal[255] = (255, 255, 255)
open('palette.bin', 'wb').write(bytes(c for rgb in pal for c in rgb))

# ---- tiles: 16x16, colour 0 = see-through ---------------------------------------------------
T = 16
def tile(fn):
    return bytes(max(0, min(255, int(fn(x, y)))) for y in range(T) for x in range(T))
tiles = [tile(lambda x, y: 0)]                                                     # 0 empty
for v in range(4):                                                                # 1-4 ground top (grass edge)
    tiles.append(tile(lambda x, y, v=v: 40 + (x * 3 + v * 5) % 6 if y < 3 + ((x + v * 3) % 5 == 0) else 3 + (x * 5 + y * 7 + v * 11) % 11))
for v in range(4):                                                                # 5-8 earth fill
    tiles.append(tile(lambda x, y, v=v: 2 + (x * 7 + y * 3 + v * 13 + (x * y) % 5) % 10))
for v in range(4):                                                                # 9-12 rock
    tiles.append(tile(lambda x, y, v=v: 17 + ((x // 4 + y // 4 + v) % 3) * 4 + (x + y * 3) % 3 if (x % 8 and y % 8) else 16))
for v in range(4):                                                                # 13-16 metal
    tiles.append(tile(lambda x, y, v=v: 62 - abs(x - 8) * 3 if v < 2 else (50 + (y % 4) * 3 if x in (0, 15) or y in (0, 15) else 56 + ((x + y) >> 2 & 1) * 4)))
for v in range(4):                                                                # 17-20 plants (see-through)
    tiles.append(tile(lambda x, y, v=v: 34 + (y + v * 2) % 10 if abs(x - 8 + math.sin((y + v * 4) * 0.7) * 3) < 1.5 + (15 - y) * 0.12 and y > v * 2 else 0))
open('tiles.bin', 'wb').write(struct.pack('<HH', len(tiles), T) + b''.join(tiles))

# ---- level: 256 x 12 tiles -------------------------------------------------------------------
LW, LH = 256, 12
lvl = [[0] * LW for _ in range(LH)]
g, c = 2, 0
for x in range(LW):
    if x % 3 == 0:
        g = max(1, min(4, g + random.choice((-1, 0, 0, 1))))
    if x > 20 and x % 5 == 0:
        c = max(0, min(3, c + random.choice((-1, 0, 1))))
    for k in range(g):
        y = LH - 1 - k
        lvl[y][x] = (1 + x % 4) if k == g - 1 else (5 + (x + y) % 4)
    for k in range(c):
        lvl[k][x] = 9 + (x + k) % 4
    if lvl[LH - g - 1][x] == 0 and x % 7 == 3:
        lvl[LH - g - 1][x] = 17 + x % 4                                           # plant on the ground
for x0 in range(30, LW - 10, 23):                                                  # floating metal platforms
    y = 4 + (x0 // 23) % 3
    for x in range(x0, x0 + 4):
        lvl[y][x] = 13 + (x - x0) % 4
for x in range(8):                                                                 # clear start area
    for y in range(LH - 2):
        lvl[y][x] = 0
spawns = []
for i in range(60):
    wx = 400 + i * 60 + random.randint(0, 30)
    spawns.append((wx, 40 + random.randint(0, 90), i % 3 == 2))
out = b'LVL1' + struct.pack('<HHHH', LW, LH, len(spawns), 0)
out += bytes(lvl[y][x] for y in range(LH) for x in range(LW))
for wx, y, kind in spawns:
    out += struct.pack('<IHH', wx, y, int(kind))
open('level.bin', 'wb').write(out)

# ---- sprites: colour 0 = see-through -------------------------------------------------------------
def spr(w, h, fn):
    return struct.pack('<HH', w, h) + bytes(max(0, min(255, int(fn(x, y)))) for y in range(h) for x in range(w))
sprites = []
def player(x, y):
    dy = abs(y - 7.5)
    if x < 4 and dy < 2 + x: return 96 + 10 + (x % 3) * 2                         # exhaust
    if dy < (x - 2) * 0.45 and dy < (34 - x) * 0.6 and x >= 4: return 66 + int(13 - dy * 1.6) - (x > 22 and dy < 2) * 3
    return 0
sprites.append(spr(32, 16, player))                                                # 0 player
for f in range(2):                                                                 # 1-2 enemy, two frames
    sprites.append(spr(16, 16, lambda x, y, f=f: 82 + int(12 - math.hypot(x - 7.5, y - 7.5) * 1.5) if math.hypot(x - 7.5, y - 7.5) < 7.5 - f * ((x + y) % 2) else 0))
sprites.append(spr(8, 4, lambda x, y: 110 if 0 < y < 3 or x in range(2, 6) else 104))  # 3 bullet
for f in range(4):                                                                 # 4-7 explosion
    r = 3 + f * 1.6
    sprites.append(spr(16, 16, lambda x, y, r=r, f=f: 110 - f * 3 - int(math.hypot(x - 7.5, y - 7.5)) if r - 2.5 - f < math.hypot(x - 7.5, y - 7.5) < r and (x * 3 + y * 5 + f) % (1 + f) == 0 else 0))
open('sprites.bin', 'wb').write(struct.pack('<H', len(sprites)) + b''.join(sprites))
print('tiles', len(tiles), 'sprites', len(sprites), 'spawns', len(spawns))
