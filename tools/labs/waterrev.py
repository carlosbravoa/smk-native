#!/usr/bin/env python3
"""Does the rev climb in deep water when the type byte is the ROAD's?
Fill the surface with $22 under a kart that was on asphalt, hold B, log."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from lab import Lab, log
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
snap = L.surface_snapshot()
L.pace(600)
log("before: spd %d rev %04X B0 %04X A0 %02X" % (L.speed(), L.w(0x10C2), L.w(0x10B0), L.b.wram[0x10A0]))
L.surface_fill(snap, 0x22)
for f in range(320):
    L.frame(0x80, 0)          # B held
    if f % 8 == 0:
        log("f%03d spd %4d rev %04X B0 %04X A0 %02X AC %02X CA %04X" % (
            f, L.speed(), L.w(0x10C2), L.w(0x10B0), L.b.wram[0x10A0], L.b.wram[0x10AC], L.w(0x10CA)))
