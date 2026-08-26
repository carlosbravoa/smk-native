"""Does the game stop DRAWING an object past the last size band?

$80C8AB parks a sprite by writing $0140 into the block's +$30 (the screen
Y the OAM gets).  So log +$06 against +$30 and read off, from the game
itself, which scales are actually on screen.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

L = Lab(settle=60)
w = L.b.wram
from collections import defaultdict
byband = defaultdict(lambda: [0, 0])
rows = []
for f in range(700):
    L.flow(1)
    for e in (0x1800, 0x1880):
        s  = w[e+6] | w[e+7] << 8
        y  = w[e+0x30] | w[e+0x31] << 8
        x  = w[e+0x18] | w[e+0x19] << 8
        if not x:
            continue
        parked = (y == 0x0140)
        band = ("hidden >=$300" if s >= 0x300 else
                ">$C0" if s > 0xC0 else ">$60" if s > 0x60 else
                ">$30" if s > 0x30 else "<=$30")
        byband[band][parked] += 1
        rows.append((s, y))
log("scale band -> on screen / parked at $0140")
for k in (">$C0", ">$60", ">$30", "<=$30", "hidden >=$300"):
    on, off = byband[k][0], byband[k][1]
    log("  %-14s on=%5d  parked=%5d" % (k, on, off))
ys = sorted(set(y for _, y in rows))
log("distinct +$30 values seen: %s%s" % (ys[:12], " ..." if len(ys) > 12 else ""))
