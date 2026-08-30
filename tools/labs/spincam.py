#!/usr/bin/env python3
"""Does the CAMERA turn with a spin?  Inject a reaction bit on P1 in the
attract race and log, per frame, the camera azimuth $94, the heading $A4,
the pose lag $AA, the pose $2A, the state $A6 and the speed."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
bit = int(sys.argv[1] if len(sys.argv) > 1 else "1000", 16)
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, sw, s16 = L.w, L.sw, L.s16
L.pace(700)
print("f,cam94,headA4,lagAA,pose2A,stA6,spd,e2")
for f in range(-3, 130):
    if f == 0:
        sw(P1 + 0xE2, w(P1 + 0xE2) | bit)
        if bit & 0x0300: sw(P1 + 0xE4, 0x2000)      # $819ACE arms the tumble rate with the bit
    print("%d,%d,%d,%d,%d,%02X,%d,%04X" % (f, w(0x94), w(P1+0xA4), s16(w(P1+0xAA)), w(P1+0x2A),
                                          L.b.wram[P1+0xA6], L.speed(), w(P1+0xE2)))
    L.frame(0x80)
