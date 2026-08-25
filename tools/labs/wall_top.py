"""A) What a real wall hit does - on the real track, not a map filled
   with wall (which put the kart inside a solid and made the numbers
   meaningless).  Aim at a genuine wall cell and record velocity and
   position straight through the impact.

B) The true top speed on a straight: hold throttle long enough to find
   the asymptote, so the target-table entry we use can be checked
   against what the game actually reaches.
"""
import math
from lab import Lab, log, P1

L = Lab()
snap = L.surface_snapshot()

log("=== A. a real wall hit ===")
if L.pace():
    x, y = L.pos()
    # scan the WHOLE map for a bit-7 cell, then take the nearest one
    best = None
    for cy in range(128):
        for cx in range(128):
            tile = L.b.wram[0x10000 + cy * 128 + cx]
            cls = L.b.wram[0x0B00 + tile] if tile < 0xC0 else 0
            if cls & 0x80:
                d = (cx * 8 - x) ** 2 + (cy * 8 - y) ** 2
                if best is None or d < best[0]:
                    best = (d, cx * 8 + 4, cy * 8 + 4, cls)
    if not best:
        log("  no bit-7 wall on this track at all")
    else:
        d, wx, wy, cls = best
        dist = math.sqrt(d)
        log("  nearest $%02X wall at (%d,%d), %.0f px away" % (cls, wx, wy, dist))
        # place the kart 90 px short of it, pointing straight at it
        a = math.atan2(wx - x, -(wy - y))
        ang = int(a * 65536 / (2 * math.pi)) & 0xFFFF
        L.sw(P1 + 0x18, int(wx - math.sin(a) * 90) & 0xFFFF)
        L.sw(P1 + 0x1C, int(wy + math.cos(a) * 90) & 0xFFFF)
        L.sw(P1 + 0x2A, ang)
        dx, dy = math.sin(a), -math.cos(a)
        x0, y0 = L.pos()
        log("    f   spd     vx     vy    fwd   state")
        for f in range(40):
            L.frame(0x80)
            cx2, cy2 = L.pos()
            fwd = (cx2 - x0) * dx + (cy2 - y0) * dy
            log("  %3d  %4d %6d %6d %6.1f  %04X"
                % (f, L.speed(), L.s16(L.w(P1 + 0x22)),
                   L.s16(L.w(P1 + 0x24)), fwd, L.w(P1 + 0x10)))
L.surface_restore(snap)

