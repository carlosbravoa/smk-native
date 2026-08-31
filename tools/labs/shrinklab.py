#!/usr/bin/env python3
"""The SMALL kart's own sprite (round 2, bug 19): poke $84 = $440 on P1 and
read the OAM entries around the player - which tiles draw the shrunk kart?"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
L.pace(400)
def kart_oam(tag):
    out = []
    for k in range(128):
        y = L.b.oam[k*4+1]; x = L.b.oam[k*4]; t = L.b.oam[k*4+2]; a = L.b.oam[k*4+3]
        if y in (0xF0, 0xE0) or y == 0: continue
        if 100 <= x <= 156 and 120 <= y <= 190:      # the P1 kart's screen box
            out.append("t%02X@(%d,%d)a%02X" % (t, x, y, a))
    log("%s: %s" % (tag, " ".join(sorted(out))))
for f in range(4): L.frame(0x80, 0)
kart_oam("BIG  ")
L.b.wram[0x1084] = 0x40; L.b.wram[0x1085] = 0x04     # $84 = $440: shrunk
for f in range(30): L.frame(0x80, 0)
kart_oam("SMALL")
for f in range(30): L.frame(0x80, 0)
kart_oam("SMALL2")
log("done; $84 now %04X" % L.w(0x1084))
