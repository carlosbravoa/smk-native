#!/usr/bin/env python3
"""Round 2, bug 19, take 2: diff the WHOLE OAM (low + high table) between
the big kart and the shrunk kart - which entries actually change?"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
L.pace(400)
for f in range(4): L.frame(0x80, 0)
def snap():
    return bytes(L.b.oam[:544])
big = snap()
L.b.wram[0x1084] = 0x40; L.b.wram[0x1085] = 0x04
for f in range(40): L.frame(0x80, 0)
small = snap()
log("$84 now %04X" % L.w(0x1084))
for k in range(128):
    b4 = big[k*4:k*4+4]; s4 = small[k*4:k*4+4]
    if b4 != s4:
        log("oam %3d: big x%3d y%3d t%02X a%02X -> small x%3d y%3d t%02X a%02X"
            % (k, b4[0], b4[1], b4[2], b4[3], s4[0], s4[1], s4[2], s4[3]))
for k in range(512, 544):
    if big[k] != small[k]:
        log("high %d: %02X -> %02X" % (k - 512, big[k], small[k]))
