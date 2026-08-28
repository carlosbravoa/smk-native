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

# ALTERNATE the phases.  A single neutral/LEFT diff is not evidence: the
# clock and the HUD churn VRAM every frame, and neutral-vs-neutral already
# differed by 162 bytes against LEFT's 234.  Bytes that are the DRIVER
# hold one value under neutral and another under LEFT, every time, so the
# test is correlation with the input rather than a single difference.
snaps = []
for name, pad in (("neutral", 0x00), ("LEFT", 0x02),
                  ("neutral", 0x00), ("LEFT", 0x02),
                  ("neutral", 0x00), ("RIGHT", 0x01)):
    log("phase: %s" % name)
    snaps.append((name, hold(pad, PHASE)))

h = [v[1][1] for v in snaps]
log("\nheading $A4 per phase: " + " ".join("$%04X" % x for x in h))
log("  the kart TURNED at a standstill: %s"
    % ("yes - that contradicts NOTES 175" if len(set(h)) > 1 else "no"))

N = [i for i, (n, _) in enumerate(snaps) if n == "neutral"]
L = [i for i, (n, _) in enumerate(snaps) if n == "LEFT"]
R = [i for i, (n, _) in enumerate(snaps) if n == "RIGHT"]
V = [v[1][0] for v in snaps]

def correlated(group):
    """bytes constant within neutral, constant within `group`, different between"""
    out = []
    for a in range(min(len(x) for x in V)):
        n0 = V[N[0]][a]
        if any(V[i][a] != n0 for i in N): continue
        g0 = V[group[0]][a]
        if any(V[i][a] != g0 for i in group): continue
        if g0 != n0: out.append(a)
    return out

for name, g in (("LEFT", L), ("RIGHT", R)):
    b = correlated(g)
    log("\n  VRAM bytes that track %s exactly: %d" % (name, len(b)))
    if b:
        runs, st = [], b[0]
        for i in range(1, len(b)):
            if b[i] != b[i-1] + 1:
                runs.append((st, b[i-1])); st = b[i]
        runs.append((st, b[-1]))
        for s_, e_ in runs[:10]:
            log("      $%04X..$%04X  (%d bytes)" % (s_, e_, e_ - s_ + 1))
log("\n  the driver's sprite changes with steering at a standstill: %s"
    % ("YES" if correlated(L) else "no - the game does not lean either"))
