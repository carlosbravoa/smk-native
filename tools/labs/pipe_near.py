"""The near pipe, measured from the game instead of from the sheet.

Rather than teleport the kart (which the race code fights), drive normally
and each frame move entity 0 to sit a chosen distance straight ahead of
the kart.  Then read what the game draws: the live tile list in the
entity's paired blocks and the OAM entries with their sizes and box.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
from smktool.screen import sprite_entries, render_sprites
from smktool.gfx import write_png

L = Lab(settle=60)
P1 = 0x1000

def objs():
    out = []
    for i, x, y, t9, a, big in sprite_entries(bytes(L.b.oam)):
        if y >= 0xE0 or y == 0:
            continue
        if 0xC0 <= (t9 & 0x1FF) <= 0xFF and ((a >> 1) & 7) == 7:
            out.append((i, x, y, t9 & 0x1FF, a, big))
    return out

def tiles(blk):
    return [L.b.wram[blk + 0x0A + i] for i in range(10)]

def put(blk, x, y):
    L.b.wram[blk + 0x18] = x & 0xFF
    L.b.wram[blk + 0x19] = (x >> 8) & 0xFF
    L.b.wram[blk + 0x1C] = y & 0xFF
    L.b.wram[blk + 0x1D] = (y >> 8) & 0xFF

L.flow(120)
log("kart at %s speed %d" % (L.pos(), L.speed()))
best = None
for dist in (256, 224, 192, 160, 128, 112, 96, 80, 72, 64, 56, 48):
    for _ in range(8):
        kx, ky = L.pos()
        h = L.heading() * 2 * math.pi / 65536
        ex = int(kx + dist * math.sin(h))
        ey = int(ky - dist * math.cos(h))
        put(0x1800, ex, ey)
        put(0x1840, ex, ey)
        L.flow(1)
    o = objs()
    log("dist %3d: %d obj sprites  blk0=%s  blk1=%s" %
        (dist, len(o),
         " ".join("%02X" % t for t in tiles(0x1800)[:6]),
         " ".join("%02X" % t for t in tiles(0x1840)[:6])))
    if not o:
        continue
    y0 = min(e[2] for e in o); y1 = max(e[2] for e in o)
    x0 = min(e[1] for e in o); x1 = max(e[1] for e in o)
    hh = (y1 - y0) + (16 if any(e[5] for e in o) else 8)
    ww = (x1 - x0) + (16 if any(e[5] for e in o) else 8)
    log("     box %d x %d px" % (ww, hh))
    if best is None or hh > best[0]:
        best = (hh, ww, dist, o, tiles(0x1800), tiles(0x1840),
                bytes(L.b.oam), bytes(L.b.vram), bytes(L.b.cgram))

if best is None:
    log("never saw the entity drawn"); raise SystemExit(1)
hh, ww, dist, o, t0, t1, oam, vram, cgram = best
log("\nLARGEST draw: dist %d -> %d px wide x %d px tall, %d sprites"
    % (dist, ww, hh, len(o)))
log("block $1800 tiles: %s" % " ".join("%02X" % t for t in t0))
log("block $1840 tiles: %s" % " ".join("%02X" % t for t in t1))
log("  idx    x    y  tile  big  pal")
for i, x, y, t9, a, big in sorted(o, key=lambda e: (e[2], e[1])):
    log("  %3d %4d %4d  $%03X   %d    %d" % (i, x, y, t9, big, (a >> 1) & 7))
W, H, buf = render_sprites(vram, cgram, oam, 0x02)
write_png("/tmp/nearpipe.png", W, H, buf)
log("frame -> /tmp/nearpipe.png")
