"""Does a slide stop the kart TURNING, or only make the velocity lag?

Our port collapses the heading's turn rate once slip passes a threshold,
which reads as an on/off switch and cannot recover.  This records, per
frame of a sustained full-lock turn, BOTH:

    d(heading)/frame      - is the kart still rotating?
    d(velocity dir)/frame - is the velocity following?
    slip                  - the gap between them

so the authority-vs-slip relationship can be read off directly instead
of assumed.
"""
import math
from lab import Lab, log, P1

L = Lab()

def vdir():
    vx, vy = L.s16(L.w(P1 + 0x22)), L.s16(L.w(P1 + 0x24))
    if not (vx or vy):
        return None
    return int(math.atan2(vx, -vy) * 65536 / (2 * math.pi)) % 65536

def sdiff(a, b):
    d = (a - b) & 0xFFFF
    return d - 65536 if d > 32768 else d

for name, hold in (("full lock, throttle held", 0x82),
                   ("full lock, no throttle",   0x02)):
    if not L.pace():
        log("%s: no pace (%d)" % (name, L.speed()))
        continue
    log("")
    log("=== %s (pace %d) ===" % (name, L.speed()))
    ph, pv = L.heading(), vdir()
    log("   f   spd    slip   dHead  dVel")
    for f in range(120):
        L.frame(hold)
        h, v = L.heading(), vdir()
        dh = sdiff(h, ph)
        dv = sdiff(v, pv) if (v is not None and pv is not None) else 0
        if f % 6 == 0 or f < 8:
            log("  %3d  %4d  %6d  %6d %6d"
                % (f, L.speed(), L.slip(), dh, dv))
        ph, pv = h, v
    L.flow(220)
