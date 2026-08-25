"""How does the game arrange the pipe's ten tiles?

We know the tiles ($CE-$D7) but assumed a 2-wide row-major grid, and
in-game the two halves come out vertically offset.  The arrangement is
in the OAM entries the game emits: each piece carries its own x,y.
"""
from lab import Lab, log

L = Lab()
found = {}
for f in range(600):
    L.frame(0x80)
    ents = []
    for i2 in range(128):
        x, yy, t, at = L.b.oam[i2 * 4:i2 * 4 + 4]
        if yy >= 0xE0:
            continue
        hi = L.b.oam[512 + (i2 >> 2)]
        xh = (hi >> ((i2 & 3) * 2)) & 1
        big = (hi >> ((i2 & 3) * 2 + 1)) & 1
        tt = t | ((at & 1) << 8)
        if 0xC0 <= (tt & 0x1FF) <= 0xDF:
            sx = x | (xh << 8)
            if sx >= 256:
                sx -= 512
            ents.append((i2, sx, yy, tt & 0x1FF, at, big))
    if len(ents) >= 4:
        found = ents
        log("frame %d: %d entity OAM entries" % (f, len(ents)))
        break

if not found:
    log("no entity sprites drawn in 600 frames")
else:
    x0 = min(e[1] for e in found)
    y0 = min(e[2] for e in found)
    log("  idx   x   y  tile  big  -> offset from top-left")
    for (i2, sx, yy, t, at, big) in sorted(found, key=lambda e: (e[2], e[1])):
        log("  %3d %4d %3d  $%02X   %d    (+%d,+%d)"
            % (i2, sx, yy, t, big, sx - x0, yy - y0))
