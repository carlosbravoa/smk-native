#!/usr/bin/env python3
"""The star's clock (round 2, bug 18): poke a READY star, press A, and log
$86 with the class under the kart - on the road, then on grass - to pin
the 'pauses at 1 below $52' reading."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
snap = L.surface_snapshot()
L.pace(400)
def cls():
    return L.b.wram[0x1068]     # $68,x: the class under the kart
L.b.wram[0x0D70] = 0x02; L.b.wram[0x0D71] = 0xC0     # READY star
L.frame(0x80, 0x80)                                   # A: use it
log("pressed; item word %04X" % L.w(0x0D70))
last = -1
for f in range(700):
    L.frame(0x80, 0)
    v = L.w(0x1086)
    if f % 20 == 0 or (v != last and v <= 3):
        log("f%03d $86 %04X class %02X spd %d" % (f, v, cls(), L.speed()))
    last = v
log("--- refill, and the same on GRASS ($5A) ---")
L.surface_fill(snap, 0x5A)
L.b.wram[0x0D70] = 0x02; L.b.wram[0x0D71] = 0xC0
L.frame(0x80, 0x80)
last = -1
for f in range(700):
    L.frame(0x80, 0)
    v = L.w(0x1086)
    if f % 20 == 0 or (v != last and v <= 3):
        log("f%03d $86 %04X class %02X spd %d" % (f, v, cls(), L.speed()))
    last = v
