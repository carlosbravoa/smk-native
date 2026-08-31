#!/usr/bin/env python3
"""$22 fall-in, the long window: the crawl's cap, $CA's end, the rescue."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
snap = L.surface_snapshot()
L.pace(600)
L.surface_fill(snap, 0x22)
for f in range(500):
    L.frame(0x80, 0)
    if f % 8 == 0:
        log("f%03d spd %4d A6 %02X A0 %02X CA %04X z %04X E2 %04X drive %02X pose %04X" % (
            f, L.speed(), L.b.wram[0x10A6], L.b.wram[0x10A0], L.w(0x10CA),
            L.w(0x101F), L.w(0x10E2), L.b.wram[0x10AC], L.w(0x102A)))
