#!/usr/bin/env python3
"""Generates assets.ntasm: every table and picture used by Nexora Kart.

All art is original and procedural (no third-party assets). The output is a
fragment of NTASM data sections included in the single game module: byte
arrays become `array<u8,N>` string initialisers and wider tables are read by
the game through `adopt`.
"""
import math
import struct
import sys
from pathlib import Path

# ----------------------------------------------------------------- palette
PAL = [(0, 0, 0)] * 256


def setpal(i, rgb):
    PAL[i] = tuple(max(0, min(255, int(c))) for c in rgb)


def ramp(start, count, a, b):
    for k in range(count):
        t = k / max(1, count - 1)
        setpal(start + k, [a[j] + (b[j] - a[j]) * t for j in range(3)])


# UI 0-15
UI = {
    "black": 0, "white": 1, "ink": 2, "yellow": 3, "red": 4, "darkred": 5,
    "blue": 6, "navy": 7, "green": 8, "gray": 9, "light": 10, "orange": 11,
    "purple": 12, "cyan": 13, "pink": 14, "brown": 15,
}
for name, rgb in [("black", (0, 0, 0)), ("white", (255, 255, 255)),
                  ("ink", (16, 16, 24)), ("yellow", (255, 214, 40)),
                  ("red", (226, 40, 40)), ("darkred", (120, 16, 24)),
                  ("blue", (40, 110, 240)), ("navy", (16, 30, 90)),
                  ("green", (40, 200, 70)), ("gray", (90, 90, 100)),
                  ("light", (190, 190, 200)), ("orange", (255, 140, 20)),
                  ("purple", (150, 60, 220)), ("cyan", (60, 220, 230)),
                  ("pink", (255, 120, 190)), ("brown", (120, 72, 30))]:
    setpal(UI[name], rgb)

ROAD = 16          # 16..19 asphalt shades
LINE = 20          # white paint
CURB_R, CURB_W = 21, 22
FIN_W, FIN_B = 23, 24
BOOST_A, BOOST_B = 25, 26
GRID = 27
ramp(ROAD, 4, (92, 92, 104), (112, 112, 124))
setpal(LINE, (232, 232, 232))
setpal(CURB_R, (214, 36, 36))
setpal(CURB_W, (240, 240, 240))
setpal(FIN_W, (250, 250, 250))
setpal(FIN_B, (20, 20, 20))
setpal(BOOST_A, (255, 200, 0))
setpal(BOOST_B, (255, 110, 0))
setpal(GRID, (200, 200, 210))
GRASS = 32         # 32 light, 33 dark, 34/35 rough
setpal(32, (88, 184, 64))
setpal(33, (70, 160, 52))
setpal(34, (60, 130, 46))
setpal(35, (52, 116, 40))
SAND = 48
setpal(48, (226, 196, 120))
setpal(49, (206, 172, 98))
WATER = 64
setpal(64, (40, 120, 220))
setpal(65, (60, 150, 240))
TREE = 66
setpal(66, (30, 90, 40))
setpal(67, (22, 70, 30))
SKY = 80           # 80..95 gradient top -> horizon
ramp(SKY, 16, (40, 90, 200), (170, 215, 255))
MOUNT_FAR, MOUNT_NEAR, SNOW, CLOUD = 96, 97, 98, 99
setpal(MOUNT_FAR, (120, 110, 170))
setpal(MOUNT_NEAR, (70, 120, 90))
setpal(SNOW, (240, 244, 255))
setpal(CLOUD, (250, 250, 255))
# Kart colours: 4 skins x 4 shades starting at 112.
SKINS = [((255, 70, 60), "red"), ((60, 200, 90), "green"),
         ((70, 120, 255), "blue"), ((255, 200, 40), "yellow")]
for s, (base, _) in enumerate(SKINS):
    ramp(112 + s * 4, 4, [c * 0.45 for c in base], [min(255, c * 1.2 + 30) for c in base])
TYRE, METAL, FACE, VISOR, SHADOW = 176, 177, 178, 179, 180
setpal(TYRE, (26, 26, 30))
setpal(METAL, (170, 170, 180))
setpal(FACE, (250, 205, 160))
setpal(VISOR, (40, 50, 80))
setpal(SHADOW, (40, 60, 40))

# Surface class of each palette index: 0 road, 1 grass, 2 sand, 3 boost,
# 4 wall/water.
MATERIAL = [1] * 256
for i in range(16, 32):
    MATERIAL[i] = 0
MATERIAL[BOOST_A] = MATERIAL[BOOST_B] = 3
for i in range(32, 48):
    MATERIAL[i] = 1
for i in range(48, 64):
    MATERIAL[i] = 2
for i in range(64, 80):
    MATERIAL[i] = 4

# ------------------------------------------------------------------- track
W = 512
CONTROL = [(112, 400), (112, 250), (130, 140), (200, 90), (286, 100),
           (318, 166), (294, 236), (330, 294), (394, 284), (406, 190),
           (396, 112), (432, 58), (466, 94), (470, 250), (464, 392),
           (416, 450), (320, 440), (250, 380), (190, 450)]
HALF = 22.0


def catmull(points, samples):
    out = []
    n = len(points)
    for i in range(n):
        p0, p1, p2, p3 = (points[(i + k - 1) % n] for k in range(4))
        for s in range(samples):
            t = s / samples
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[j]) + (-p0[j] + p2[j]) * t +
                                    (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j]) * t2 +
                                    (-p0[j] + 3 * p1[j] - 3 * p2[j] + p3[j]) * t3)
                             for j in range(2)))
    return out


CENTER = catmull(CONTROL, 48)
N_CENTER = len(CENTER)
# cumulative length for evenly spaced waypoints
LENGTHS = [0.0]
for i in range(1, N_CENTER + 1):
    a, b = CENTER[i - 1], CENTER[i % N_CENTER]
    LENGTHS.append(LENGTHS[-1] + math.dist(a, b))
TOTAL = LENGTHS[-1]


def at_length(d):
    d %= TOTAL
    lo = 0
    while LENGTHS[lo + 1] < d:
        lo += 1
    a, b = CENTER[lo], CENTER[(lo + 1) % N_CENTER]
    t = (d - LENGTHS[lo]) / max(1e-9, LENGTHS[lo + 1] - LENGTHS[lo])
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


N_WAY = 64
WAYPOINTS = [at_length(TOTAL * k / N_WAY) for k in range(N_WAY)]


def curvature(k):
    a, b, c = WAYPOINTS[k - 1], WAYPOINTS[k], WAYPOINTS[(k + 1) % N_WAY]
    h1 = math.atan2(b[1] - a[1], b[0] - a[0])
    h2 = math.atan2(c[1] - b[1], c[0] - b[0])
    return abs((h2 - h1 + math.pi) % (2 * math.pi) - math.pi)


BUCKET = 16
BUCKETS = {}
for _i in range(N_CENTER):
    _a, _b = CENTER[_i], CENTER[(_i + 1) % N_CENTER]
    for _by in range(int(min(_a[1], _b[1]) - 48) // BUCKET, int(max(_a[1], _b[1]) + 48) // BUCKET + 1):
        for _bx in range(int(min(_a[0], _b[0]) - 48) // BUCKET, int(max(_a[0], _b[0]) + 48) // BUCKET + 1):
            BUCKETS.setdefault((_bx, _by), []).append(_i)


def nearest(x, y):
    """Distance to the centre line, along-track position and side."""
    best = (1e9, 0.0, 0)
    for i in BUCKETS.get((int(x) // BUCKET, int(y) // BUCKET), ()):
        a, b = CENTER[i], CENTER[(i + 1) % N_CENTER]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length2 = dx * dx + dy * dy
        t = max(0.0, min(1.0, ((x - a[0]) * dx + (y - a[1]) * dy) / length2))
        px, py = a[0] + dx * t, a[1] + dy * t
        d = math.hypot(x - px, y - py)
        if d < best[0]:
            side = 1 if dx * (y - a[1]) - dy * (x - a[0]) > 0 else -1
            best = (d, LENGTHS[i] + math.sqrt(length2) * t, side)
    return best


def track_map():
    grid = bytearray(W * W)
    # coarse acceleration: only evaluate full distance near the centre line
    boost_marks = [TOTAL * f for f in (0.16, 0.47, 0.80)]
    for y in range(W):
        for x in range(W):
            checker = ((x >> 4) ^ (y >> 4)) & 1
            value = GRASS + checker
            if x < 6 or y < 6 or x >= W - 6 or y >= W - 6:
                value = WATER + ((x + y) >> 2 & 1)
            else:
                d, along, side = nearest(x + 0.5, y + 0.5)
                if d < HALF:
                    shade = ((x * 7 + y * 13) ^ (x * y)) & 3
                    value = ROAD + (1 if shade == 0 else 0)
                    if abs(d - 1.0) < 0.9 and int(along) % 16 < 8:
                        value = LINE
                    # finish line: checkered band across the track at 0
                    rel = (along + 6) % TOTAL
                    if rel < 8:
                        value = FIN_W if ((int(rel) >> 1) + (int(d * side + 64) >> 1)) & 1 else FIN_B
                    for mark in boost_marks:
                        r = (along - mark) % TOTAL
                        if r < 10 and d < 9 and side < 0:
                            value = BOOST_A if int(r) % 4 < 2 else BOOST_B
                    # starting grid slots behind the line
                    back = (6 - along) % TOTAL
                    if 10 < back < 60 and abs(d - 8) < 5 and int(back) % 16 < 2:
                        value = GRID
                elif d < HALF + 4:
                    value = CURB_R if int(along / 6) % 2 else CURB_W
                elif d < HALF + 10:
                    value = GRASS + 2 + checker
            grid[y * W + x] = value
    # sand traps on the outside of the sharpest corners
    for k in range(N_WAY):
        if curvature(k) > 0.36:
            wx, wy = WAYPOINTS[k]
            a, c = WAYPOINTS[k - 1], WAYPOINTS[(k + 1) % N_WAY]
            mx, my = (a[0] + c[0]) / 2, (a[1] + c[1]) / 2
            ox, oy = wx - mx, wy - my
            norm = math.hypot(ox, oy) or 1
            sx, sy = wx + ox / norm * 36, wy + oy / norm * 36
            for y in range(int(sy) - 16, int(sy) + 17):
                for x in range(int(sx) - 16, int(sx) + 17):
                    if 6 <= x < W - 6 and 6 <= y < W - 6 and math.hypot(x - sx, y - sy) < 15:
                        if MATERIAL[grid[y * W + x]] == 1:
                            grid[y * W + x] = SAND + ((x ^ y) >> 1 & 1)
    return grid


# ---------------------------------------------------------------- sprites
SPR = 32


def kart_sprite(skin, lean):
    base = 112 + skin * 4
    img = [[0] * SPR for _ in range(SPR)]

    def rect(x0, y0, x1, y1, c):
        for yy in range(max(0, y0), min(SPR, y1)):
            for xx in range(max(0, x0), min(SPR, x1)):
                img[yy][xx] = c

    def disc(cx, cy, r, c):
        for yy in range(SPR):
            for xx in range(SPR):
                if (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r:
                    img[yy][xx] = c
    o = lean * 2
    rect(3, 26, 29, 31, SHADOW)                 # ground shadow
    rect(3 - o // 2, 19, 10 - o // 2, 30, TYRE)  # rear tyres
    rect(22 - o // 2, 19, 29 - o // 2, 30, TYRE)
    rect(8, 18, 24, 28, base + 1)               # body
    rect(9, 19, 23, 21, base + 3)
    rect(6, 24, 26, 28, base)                   # bumper
    rect(12, 25, 20, 27, METAL)                 # exhaust plate
    rect(10, 14, 22, 19, base + 2)              # seat back
    disc(16 + o, 10, 6, base + 3)               # helmet
    disc(16 + o, 10, 5, base + 2)
    rect(12 + o, 9, 20 + o, 12, VISOR)
    rect(13 + o, 16, 19 + o, 19, FACE)          # neck
    rect(5 - o, 16, 27 - o, 18, base + 1)       # steering arms
    for yy in range(SPR):
        for xx in range(SPR):
            if img[yy][xx] == 0 and 0 < xx < SPR - 1 and 0 < yy < SPR - 1:
                if any(img[yy + dy][xx + dx] not in (0, SHADOW, UI["ink"])
                       for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                    img[yy][xx] = UI["ink"]      # outline
    return bytes(v for row in img for v in row)


SPRITES = b"".join(kart_sprite(s, lean) for s in range(4) for lean in (-1, 0, 1))

# ------------------------------------------------------------------- font
GLYPHS = {
    " ": [], "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11100", "10010", "10001", "10001", "10001", "10010", "11100"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "10001", "11001", "10101", "10011", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "10101", "01010"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    "!": ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    "'": ["00100", "00100", "01000", "00000", "00000", "00000", "00000"],
    "?": ["01110", "10001", "00001", "00110", "00100", "00000", "00100"],
    "<": ["00010", "00100", "01000", "10000", "01000", "00100", "00010"],
    ">": ["01000", "00100", "00010", "00001", "00010", "00100", "01000"],
    "(": ["00010", "00100", "01000", "01000", "01000", "00100", "00010"],
    ")": ["01000", "00100", "00010", "00010", "00010", "00100", "01000"],
    "=": ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
    "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
}


def font():
    out = bytearray(96 * 8)
    for ch, rows in GLYPHS.items():
        code = ord(ch) - 32
        for r, bits in enumerate(rows):
            out[code * 8 + r] = int(bits, 2)
    return bytes(out)


# ---------------------------------------------------------------- tables
HORIZON = 64
CAM_HEIGHT = 18.0
FOCAL = 160.0


def row_tables():
    """Per floor row: distance and lateral step, both 16.16 fixed point."""
    dist, step = [], []
    for y in range(200):
        if y <= HORIZON:
            dist.append(0)
            step.append(0)
            continue
        d = CAM_HEIGHT * FOCAL / (y - HORIZON + 0.5)
        dist.append(int(d * 65536))
        step.append(int(d / FOCAL * 65536))
    return dist, step


def sin_table():
    return [int(round(math.sin(2 * math.pi * i / 4096) * 65536)) for i in range(4096)]


def recip_table():
    """65536 * FOCAL / z for z in world units (index), clamped."""
    return [0] + [int(65536 * FOCAL / z) for z in range(1, 1024)]


def mountains():
    far, near = bytearray(1024), bytearray(1024)
    for x in range(1024):
        t = 2 * math.pi * x / 1024
        far[x] = int(22 + 9 * math.sin(3 * t) + 6 * math.sin(7 * t + 1) + 3 * math.sin(17 * t))
        near[x] = int(10 + 5 * math.sin(5 * t + 2) + 3 * math.sin(11 * t + 0.5) + 2 * math.sin(23 * t))
    return far, near


def minimap(track):
    out = bytearray(64 * 64)
    for y in range(64):
        for x in range(64):
            counts = {}
            for yy in range(8):
                for xx in range(8):
                    m = MATERIAL[track[(y * 8 + yy) * W + x * 8 + xx]]
                    counts[m] = counts.get(m, 0) + 1
            if counts.get(0, 0) + counts.get(3, 0) >= 12:
                out[y * 64 + x] = UI["light"]
            elif counts.get(4, 0) > 32:
                out[y * 64 + x] = 0
            else:
                out[y * 64 + x] = 0
    return bytes(out)


def keymap():
    km = bytearray(256)
    plain = {0x01: 7, 0x11: 1, 0x1F: 2, 0x1E: 3, 0x20: 4, 0x39: 5, 0x1C: 6,
             0x48: 1, 0x50: 2, 0x4B: 3, 0x4D: 4, 0x2A: 8, 0x36: 8, 0x1D: 8, 0x2D: 8}
    extended = {0x48: 1, 0x50: 2, 0x4B: 3, 0x4D: 4, 0x1C: 6, 0x1D: 8}
    for code, key in plain.items():
        km[code] = key
    for code, key in extended.items():
        km[128 + code] = key
    return bytes(km)


# ---------------------------------------------------------------- output
def literal(data):
    parts = []
    for b in data:
        if 32 <= b < 127 and b not in (34, 92):
            parts.append(chr(b))
        else:
            parts.append(f"\\x{b:02x}")
    return '"' + "".join(parts) + '"'


def text(s):
    return s.encode("ascii") + b"\x00"


def main():
    out_path = Path(sys.argv[1])
    track = track_map()
    dist, step = row_tables()
    far, near = mountains()
    start = WAYPOINTS[0]
    before = at_length(TOTAL - 30)
    heading = math.atan2(start[1] - before[1], start[0] - before[0])
    start_angle = int(round(heading / (2 * math.pi) * 4096)) % 4096
    grid = []
    for slot in range(4):
        back = 20 + 16 * slot
        px, py = at_length(TOTAL - back)
        nx, ny = -math.sin(heading), math.cos(heading)
        side = 8 if slot % 2 else -8
        grid.append((px + nx * side, py + ny * side))
    tables = {
        "palette": bytes(c for rgb in PAL for c in rgb),
        "material": bytes(MATERIAL),
        "keymap": keymap(),
        "track": bytes(track),
        "sprites": SPRITES,
        "font": font(),
        "mount_far": bytes(far),
        "mount_near": bytes(near),
        "minimap": minimap(track),
        "row_dist": struct.pack("<200i", *dist),
        "row_step": struct.pack("<200i", *step),
        "sine": struct.pack("<4096i", *sin_table()),
        "recip": struct.pack("<1024i", *recip_table()),
        "waypoints": struct.pack(f"<{N_WAY * 2}i", *[int(v * 65536) for p in WAYPOINTS for v in p]),
        "grid": struct.pack("<8i", *[int(v * 65536) for p in grid for v in p]),
        "msg_boot": text("\r\nNEXORA OS (NTASM): boot"),
        "msg_owned": text("\r\nNEXORA: firmware exited, the machine is ours\r\n"),
        "msg_fail": text("\r\nNEXORA: boot failure "),
    }
    lines = ["// Generated by tools/gen_assets.py - do not edit.",
             f"%define START_ANGLE {start_angle}",
             f"%define N_WAY {N_WAY}",
             f"%define HORIZON {HORIZON}",
             "section .rdata {"]
    for name, data in tables.items():
        lines.insert(1, f"%define {name.upper()}_LEN {len(data)}")
        lines.append(f"    data {name}:array<u8,{len(data)}> = {literal(data)}")
    lines.append("}")
    out_path.write_text("\n".join(lines) + "\n")
    Path(str(out_path) + ".map.ppm").write_bytes(
        b"P6 512 512 255\n" + bytes(c for v in track for c in PAL[v]))


if __name__ == "__main__":
    main()
