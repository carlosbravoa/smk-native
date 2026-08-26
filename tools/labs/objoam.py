"""How big does the game actually DRAW an object?

The object block carries its own screen position - $2C/$2E from $80C853
and $30 from $80C8AE - so the OAM entries belonging to it can be found by
position instead of guessed.  Count them, and read their size bits, and
the on-screen size of a pipe is a measurement rather than an argument.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

L = Lab(settle=60)
w, oam = L.b.wram, L.b.oam
log("OBSEL sizes: the ROM writes $02 -> small 8x8, large 16x16")

def sprites():
    out = []
    for i in range(128):
        y = oam[i*4+1]
        hi = oam[512 + (i >> 2)]
        sh = (hi >> ((i & 3) * 2)) & 3
        x = oam[i*4] | ((sh & 1) << 8)
        if y in (0xE0, 0xF0) or y > 224:
            continue
        out.append((x if x < 256 else x - 512, y, oam[i*4+2], oam[i*4+3], (sh >> 1) & 1))
    return out

best = None
for f in range(900):
    L.flow(1)
    for e in (0x1800, 0x1880):
        sx = w[e+0x2C] | w[e+0x2D] << 8
        sy = w[e+0x30] | w[e+0x31] << 8
        s6 = w[e+6] | w[e+7] << 8
        if sy >= 0x0140 or sy > 224 or s6 == 0 or s6 > 0x300:
            continue
        near = [s for s in sprites() if abs(s[0] - (sx & 0x1FF)) < 40 and abs(s[1] - sy) < 40]
        if near and (best is None or s6 > best[0]):
            best = (s6, sx, sy, near)
if best:
    s6, sx, sy, near = best
    log("closest object seen: +$06 = %d, screen (%d,%d)" % (s6, sx, sy))
    log("OAM entries around it: %d" % len(near))
    for x, y, t, a, big in sorted(near, key=lambda s: (s[1], s[0])):
        log("   x=%4d y=%4d tile=%3d attr=%02X size=%s" % (x, y, t, a, "16x16" if big else "8x8"))
    xs = [s[0] for s in near]; ys = [s[1] for s in near]
    log("bounding box: %d x %d px" % (max(xs) - min(xs) + 16, max(ys) - min(ys) + 16))
else:
    log("no object was on screen with a live scale")
