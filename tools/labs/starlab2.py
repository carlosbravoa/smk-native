#!/usr/bin/env python3
"""The star on classes BELOW $52 (ice $4E, road $40): does $86 pause at 1?"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
snap = L.surface_snapshot()
for cls in (0x4E, 0x40):
    L.surface_fill(snap, cls)
    L.b.wram[0x0D70] = 0x02; L.b.wram[0x0D71] = 0xC0
    L.frame(0x80, 0x80)
    log("=== fill %02X, star pressed (word %04X)" % (cls, L.w(0x0D70)))
    for f in range(560):
        L.frame(0x80, 0)
        v = L.w(0x1086)
        if f % 40 == 0 or v in (2, 1):
            log("f%03d $86 %04X class %02X" % (f, v, L.b.wram[0x1068]))
        if v == 0 and f > 520: break
