#!/usr/bin/env python3
"""What does the GAME do on class $22 at speed?  surface_fill($22) under a
flowing kart and log the sink/skim signature (round 2: 'sink on water is
still not working - anywhere')."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
snap = L.surface_snapshot()
L.pace(600)
log("speed before fill: %d" % L.speed())
L.surface_fill(snap, 0x22)
for f in range(120):
    L.frame(0x80, 0)
    if f % 4 == 0:
        log("f%03d spd %4d A6 %02X CA %04X z %04X zv %04X E2 %04X drive %02X" % (
            f, L.speed(), L.b.wram[0x10A6], L.w(0x10CA),
            L.w(0x101F), L.w(0x1026), L.w(0x10E2), L.b.wram[0x10AC]))
