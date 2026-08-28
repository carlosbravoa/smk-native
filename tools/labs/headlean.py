#!/usr/bin/env python3
"""Does the driver lean at a STANDSTILL, and what changes when he does?

    tools/labs/headlean.py

The user: "When stopped (speed=0) and you press left or right, the cart
doesn't turn, the player only leans their head left or right.  Nothing
else."  The port already refuses to turn - $80:A9B8[0] is 0 for speeds
0..15 (NOTES 175) - so the open half is the SPRITE.

Asked of the running machine rather than of a sheet, which is the lesson
of S28: hold the kart at zero speed, run three phases (neutral, LEFT,
RIGHT), and diff VRAM between them.  Whatever bytes differ ARE the
driver's sprite, and if nothing differs the game does not lean either.
VRAM is the oracle's to read; MAME would show none of this.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

P1 = 0x1000
PHASE = int(os.environ.get("PHASE", "40"))

L = Lab(settle=90)
w = L.b.wram

def hold(pad, n):
    """n frames at zero speed with `pad` held, then a VRAM snapshot"""
    for _ in range(n):
        L.sw(P1 + 0xEA, 0)                  # speed
        L.sw(P1 + 0x22, 0); L.sw(P1 + 0x24, 0)   # velocity
        L.frame(pad)
    return bytes(L.b.vram), L.w(P1 + 0xA4), L.w(P1 + 0x2A)

log("phase 1: neutral")
v0, h0, p0 = hold(0x00, PHASE)
log("phase 2: LEFT held")
vL, hL, pL = hold(0x02, PHASE)
log("phase 3: RIGHT held")
vR, hR, pR = hold(0x01, PHASE)
log("phase 4: neutral again")
v1, h1, p1 = hold(0x00, PHASE)

log("\nheading $A4 and pose $2A through the phases:")
log("  neutral $%04X/$%04X   LEFT $%04X/$%04X   RIGHT $%04X/$%04X   back $%04X/$%04X"
    % (h0, p0, hL, pL, hR, pR, h1, p1))
log("  the kart TURNED at a standstill: %s"
    % ("yes - that contradicts NOTES 175" if len({h0, hL, hR}) > 1 else "no"))

def diff(a, b, name):
    runs, start = [], None
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            if start is None: start = i
        elif start is not None:
            runs.append((start, i)); start = None
    if start is not None: runs.append((start, len(a)))
    total = sum(e - s for s, e in runs)
    log("  %-18s %5d bytes differ in %d run(s)" % (name, total, len(runs)))
    for s, e in runs[:8]:
        log("      VRAM $%04X..$%04X  (%d bytes)" % (s, e - 1, e - s))
    return total

log("\nVRAM differences between phases:")
nL = diff(v0, vL, "neutral vs LEFT")
nR = diff(v0, vR, "neutral vs RIGHT")
nB = diff(v0, v1, "neutral vs neutral")
log("\n  the driver's sprite CHANGES when steering at a standstill: %s"
    % ("yes" if (nL or nR) and nL + nR > nB else "no - the game does not lean either"))
