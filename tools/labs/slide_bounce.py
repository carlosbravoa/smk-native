"""Two measurements the playtest asked for:

A) where does the slip angle SATURATE in a sustained slide?  (our plow
   grew it without bound, so the kart ended up travelling sideways)
B) how far does a wall bounce actually move the kart?  (ours threw the
   kart "a few meters"; the real one is short)
"""
import math
from lab import Lab, log, P1

L = Lab()

log("=== A. sustained slide ===")
for name, hop in (("hard turn (no hop)", False), ("hop-drift", True)):
    if not L.pace():
        log("%s: could not reach pace (%d)" % (name, L.speed()))
        continue
    log("%s: pace %d" % (name, L.speed()))
    rows = []
    for f in range(150):
        lo = 0x20 if (hop and f < 4) else 0
        L.frame(0x82, lo)                     # B + Left, full lock
        rows.append((f, L.speed(), L.slip(), L.w(P1 + 0xE2)))
    L.b.reg_reads[0x4218] = 0
    for f in (5, 10, 20, 30, 45, 60, 90, 120, 149):
        n, sp, sl, e2 = rows[f]
        log("   f%3d  spd %4d  slip %6d (%5.1f deg)  E2 %04X"
            % (n, sp, sl, sl * 360.0 / 65536, e2))
    sl = [abs(x[2]) for x in rows]
    log("   slip peak %d (%.1f deg)  final %d (%.1f deg)"
        % (max(sl), max(sl) * 360.0 / 65536, sl[-1], sl[-1] * 360.0 / 65536))
    L.flow(200)

log("")
log("=== B. wall bounce ===")
snap = L.surface_snapshot()
if L.pace():
    x0, y0 = L.pos()
    a = L.heading() * 2 * math.pi / 65536.0
    dx, dy = math.sin(a), -math.cos(a)
    s0 = L.speed()
    L.surface_fill(snap, 0x80)                 # a wall right in front
    log("  approach speed %d" % s0)
    log("    f  spd    fwd    lat  state")
    for f in range(30):
        L.frame(0x80)
        x, y = L.pos()
        fwd = (x - x0) * dx + (y - y0) * dy
        lat = (x - x0) * dy - (y - y0) * dx
        if f % 2 == 0:
            log("  %3d %4d %6.1f %6.1f  %04X"
                % (f, L.speed(), fwd, lat, L.w(P1 + 0x10)))
    L.surface_restore(snap)
else:
    log("  could not reach pace")
