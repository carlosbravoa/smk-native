"""S10: what does the game DRAW for a given object scale (+$06)?

NOTES 105 measured the law (+$06 = $4200 / distance-from-the-kart) but
not what the game does with it, and assumed the demo never draws
entities.  It does: track 7 keeps two live object blocks ($1800/$1880,
NOTES 127) and their +$06 moves as you drive.  So capture +$06 next to
the OAM the game emits, and read the size ladder straight off it.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

L = Lab(settle=60)
w, oam = L.b.wram, L.b.oam

def kart():
    return (L.s16(L.w(P1 + 0x18)), L.s16(L.w(P1 + 0x1C)))

def objs():
    out = []
    for e in (0x1800, 0x1880):
        x = w[e+0x18] | w[e+0x19] << 8
        y = w[e+0x1C] | w[e+0x1D] << 8
        s = w[e+6] | w[e+7] << 8
        if x or y:
            out.append((x, y, s))
    return out

def sprites():
    """visible OAM entries: (x, y, tile, attr, big)"""
    out = []
    for i in range(128):
        y = oam[i*4+1]
        if y in (0, 0xE0, 0xF0):
            continue
        hi = oam[512 + (i >> 2)]
        sh = (hi >> ((i & 3) * 2)) & 3
        out.append((oam[i*4] | ((sh & 1) << 8), y, oam[i*4+2], oam[i*4+3], (sh >> 1) & 1))
    return out

seen = {}
log("frame  d0   +$06   tiles in use (tile:count) for the non-kart bands")
for f in range(900):
    L.flow(1)
    o = objs()
    if not o:
        continue
    kx, ky = kart()
    for (ox, oy, s) in o:
        d = math.hypot(ox - kx, oy - ky)
        if s == 0:
            continue
        pred = 0x4200 / d if d else 0
        key = s
        if key not in seen:
            seen[key] = (round(d), round(pred), f)
log("+$06 -> (measured distance, $4200/d, first frame)")
for k in sorted(seen):
    d, p, fr = seen[k]
    log("  %3d (0x%02X):  d=%4d   $4200/d=%4d   err %+d   f%d" % (k, k, d, p, p - k, fr))
sp = sprites()
log("visible OAM entries at the end: %d" % len(sp))
from collections import Counter
log("tile histogram: %s" % Counter(t for _, _, t, _, _ in sp).most_common(12))
log("size bits: %s" % Counter(b for *_, b in sp).most_common())
