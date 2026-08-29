#!/usr/bin/env python3
"""The no-coin bump spin, measured on the running game.

    tools/labs/bumpspin.py [0800|1000]      default 0800

A kart-kart bump gives the kart with no coins $E2 |= $0800 ($819AF5), and
the reaction dispatcher at $80B49D sends that bit to $80B435: state $0E
or $10 with nothing else set - no speed clamp, no $FA, the drive state
untouched.  $1000 is the banana for comparison.  B is held the whole way,
so what the speed does is the reaction itself, not the throttle.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

P1 = 0x1000
bit = int(sys.argv[1] if len(sys.argv) > 1 else "0800", 16)
FRAMES = int(os.environ.get("FRAMES", "160"))
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, sw, s16 = L.w, L.sw, L.s16
if not L.pace(700): log("could not get up to speed")
log("moving at %d, coins %d, $A6=$%02X $AC=$%02X" % (L.speed(), w(0x0E00), L.b.wram[P1+0xA6], L.b.wram[P1+0xAC]))
sw(0x0E00, 0)
sw(P1 + 0xE2, w(P1 + 0xE2) | bit)
print("f,spd,a6,ac,aa,a8,e2,fa,c22,c24,e4")
for f in range(FRAMES):
    print("%d,%d,%02X,%02X,%d,%d,%04X,%d,%d,%d,%04X" % (
        f, L.speed(), L.b.wram[P1+0xA6], L.b.wram[P1+0xAC], s16(w(P1+0xAA)), s16(w(P1+0xA8)),
        w(P1+0xE2), s16(w(P1+0xFA)), s16(w(P1+0x22)), s16(w(P1+0x24)), w(P1+0xE4)))
    L.frame(0x80)
