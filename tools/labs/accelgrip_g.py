"""Battery G: the grip curve - what a full-lock turn does at each speed.

    python3 tools/labs/accelgrip_g.py

"Grip" in the driver's sense is two numbers: how tight the kart turns, and
how far the velocity lags where it points.  Both are functions of SPEED in
this game, so they are swept over speed on a featureless road, with
everything else held still.  `tools/accelgrip.c` runs the identical sweep
on our side.
"""
import math
from lab import Lab, log, P1

L = Lab()
snap = L.surface_snapshot()
for i in range(0xC0):
    L.b.wram[0x0B00 + i] = 0x40


def st(a):
    return L.s16(L.w(P1 + a))


def park():
    L.sw(P1 + 0x18, 512); L.sw(P1 + 0x1A, 0)
    L.sw(P1 + 0x1C, 512); L.sw(P1 + 0x1E, 0)


log("character $C0 %02X  base top $B4 %d  target $D6 %d  class $30 %d"
    % (L.b.wram[P1 + 0xC0], st(0xB4), st(0xD6), L.w(0x0030)))
log("")
log("=== G. full-lock turn at each speed (120 frames, B + Left) ===")
log("   speed  slide  turn/f(deg)  radius(px)  lag $A8 (deg)  spinout f  end spd")
for s in (200, 300, 400, 500, 600, 700, 784, 850, 900, 912, 940, st(0xD6)):
    park()
    L.sw(P1 + 0xE8, 0); L.sw(P1 + 0xEA, s)
    L.sw(P1 + 0xEC, 0); L.sw(P1 + 0xEE, 0)
    for a in (0xA8, 0xAA, 0xA6, 0xFA, 0xB2):
        L.sw(P1 + a, 0)
    lag = 0
    spin_at = None
    rates = []
    prev_h = L.w(P1 + 0xA4)
    for f in range(120):
        park()
        L.frame(0x82)
        h = L.w(P1 + 0xA4)
        d = (h - prev_h) & 0xFFFF
        rates.append(d - 65536 if d > 32768 else d)
        prev_h = h
        lag = max(lag, abs(st(0xA8)))
        if L.w(P1 + 0xA6) in (0x0E, 0x10) and spin_at is None:
            spin_at = f
    rate = sum(rates[-20:]) / 20.0
    rad = abs((s / 256.0) / (rate * 2 * math.pi / 65536.0)) if rate else 0
    log("   %5d   %s   %8.2f   %9.1f   %6d(%5.1f)      %6s   %5d"
        % (s, "yes" if lag else " no ", rate * 360.0 / 65536, rad,
           lag, lag * 360.0 / 65536,
           spin_at if spin_at is not None else "-", st(0xEA)))
L.surface_restore(snap)
