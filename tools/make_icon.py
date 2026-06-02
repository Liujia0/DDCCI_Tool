#!/usr/bin/env python3
"""Generate app.ico for DDCCI Monitor Tool.

Rasterizes the same design as web/logo.svg (a monitor screen with a brightness
"sun" control motif on a dark rounded badge) at several sizes and packs them
into a PNG-in-ICO file. No third-party image libraries required.

Run from the project root:  python3 tools/make_icon.py
"""
import math
import struct
import zlib

CANVAS = 256.0

# ---- colors (r,g,b) ----
BADGE_TOP = (0x2a, 0x2a, 0x40)
BADGE_BOT = (0x1c, 0x1c, 0x2a)
ACCENT    = (0x5b, 0x8d, 0xf0)
SCREEN_TOP = (0x1a, 0x1a, 0x28)
SCREEN_BOT = (0x11, 0x11, 0x1b)
SUN_IN    = (0xff, 0xe6, 0xa0)
SUN_OUT   = (0xff, 0xc8, 0x5b)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def rrect_inside(x, y, cx, cy, hw, hh, r):
    dx = max(abs(x - cx) - (hw - r), 0.0)
    dy = max(abs(y - cy) - (hh - r), 0.0)
    return (dx * dx + dy * dy) <= r * r


def circle_inside(x, y, cx, cy, r):
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def seg_dist(px, py, x1, y1, x2, y2):
    dx, dy = x2 - x1, y2 - y1
    l2 = dx * dx + dy * dy
    if l2 == 0:
        return math.hypot(px - x1, py - y1)
    t = max(0.0, min(1.0, ((px - x1) * dx + (py - y1) * dy) / l2))
    return math.hypot(px - (x1 + t * dx), py - (y1 + t * dy))


# brightness sun rays (matching the SVG), as line segments
RAYS = [
    (128, 80, 128, 90), (128, 134, 128, 144),
    (100, 112, 90, 112), (166, 112, 156, 112),
    (108.4, 92.4, 101.3, 85.3), (154.7, 138.7, 147.6, 131.6),
    (147.6, 92.4, 154.7, 85.3), (101.3, 138.7, 108.4, 131.6),
]


def sample(x, y):
    """Return topmost (r,g,b,a) at canvas coordinate (x,y)."""
    # sun core + rays (top)
    if circle_inside(x, y, 128, 112, 17):
        t = math.hypot(x - 128, y - 112) / 17.0
        return lerp(SUN_IN, SUN_OUT, min(t, 1.0)) + (255,)
    for (x1, y1, x2, y2) in RAYS:
        if seg_dist(x, y, x1, y1, x2, y2) <= 3.5:
            return SUN_OUT + (255,)
    # screen inner
    if rrect_inside(x, y, 128, 112, 66, 44, 10):
        t = (y - 68) / 88.0
        return lerp(SCREEN_TOP, SCREEN_BOT, max(0.0, min(1.0, t))) + (255,)
    # screen body (bezel)
    if rrect_inside(x, y, 128, 112, 78, 56, 18):
        return ACCENT + (255,)
    # stand base
    if rrect_inside(x, y, 128, 194, 44, 8, 8):
        return ACCENT + (255,)
    # stand neck
    if rrect_inside(x, y, 128, 177, 10, 11, 4):
        return ACCENT + (255,)
    # badge
    if rrect_inside(x, y, 128, 128, 116, 116, 52):
        t = (y - 12) / 232.0
        return lerp(BADGE_TOP, BADGE_BOT, max(0.0, min(1.0, t))) + (255,)
    return (0, 0, 0, 0)


def render(size, ss):
    px = bytearray(size * size * 4)
    inv = CANVAS / size
    for j in range(size):
        for i in range(size):
            ar = ag = ab = aa = 0.0
            for sj in range(ss):
                cy = (j + (sj + 0.5) / ss) * inv
                for si in range(ss):
                    cx = (i + (si + 0.5) / ss) * inv
                    r, g, b, a = sample(cx, cy)
                    af = a / 255.0
                    ar += r * af
                    ag += g * af
                    ab += b * af
                    aa += af
            n = ss * ss
            o = (j * size + i) * 4
            if aa > 0:
                px[o]     = min(255, int(round(ar / aa)))
                px[o + 1] = min(255, int(round(ag / aa)))
                px[o + 2] = min(255, int(round(ab / aa)))
            px[o + 3] = min(255, int(round(aa / n * 255)))
    return bytes(px)


def png(size, rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
    raw = bytearray()
    for j in range(size):
        raw.append(0)  # filter: none
        raw += rgba[j * size * 4:(j + 1) * size * 4]
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def main():
    specs = [(16, 4), (32, 4), (48, 4), (64, 4), (128, 3), (256, 2)]
    images = []
    for size, ss in specs:
        images.append((size, png(size, render(size, ss))))
        print("rendered", size)

    out = bytearray()
    out += struct.pack("<HHH", 0, 1, len(images))  # ICONDIR
    offset = 6 + 16 * len(images)
    for size, data in images:
        w = h = 0 if size == 256 else size
        out += struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in images:
        out += data

    with open("app.ico", "wb") as f:
        f.write(out)
    print("wrote app.ico", len(out), "bytes")


if __name__ == "__main__":
    main()
