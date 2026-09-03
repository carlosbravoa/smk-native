#!/usr/bin/env python3
"""Which spin states turn the CAMERA?  NOTES 196 measured the object
tumble ($1A) at $A4 + $C0 + $AA/2 and the banana at $A4 + $C0 flat; the
feather ($18) came out flat too (tools/labs/feathercam.py).  This pokes
the SLIDE SPIN-OUT states ($0E and $10) and logs the same three numbers,
so the port can stop applying one state's rule to all six."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
w, s16 = L.w, L.s16
for st in (0x0E, 0x10):
    L.pace(600)
    L.sw(P1 + 0xA6, st)
    L.sw(P1 + 0xAA, 0)
    log("--- state $%02X ---" % st)
    log("   f  $94 cam  $A4 head  $AA lag   $A6   cam-head")
    for f in range(24):
        cam, head, lag = w(0x94), w(P1 + 0xA4), s16(w(P1 + 0xAA))
        d = (cam - head) & 0xFFFF
        if d > 32768: d -= 65536
        log("  %2d  %6d  %6d  %6d   %02X   %+6d"
            % (f, cam, head, lag, L.b.wram[P1 + 0xA6], d))
        L.frame(0x80)
