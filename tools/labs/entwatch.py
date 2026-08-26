"""Do the object blocks move on the demo's own track, and where?

Cheap version of entmotion.py: the plain Lab (track 7) boots in a minute.
Scan a wide WRAM window for words that change over 120 frames AND look
like world coordinates, so the object blocks find themselves.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

L = Lab(settle=60)
w = L.b.wram
log("track $0124 = $%02X  $0D2C = $%02X  $1EE4 = $%04X"
    % (w[0x0124], w[0x0D2C], w[0x1EE4] | w[0x1EE5] << 8))

LO, HI = 0x1000, 0x2000
before = bytes(w[LO:HI])
for _ in range(120):
    L.frame(0x80)
after = bytes(w[LO:HI])

runs = []
for a in range(0, HI - LO - 1, 2):
    v0 = before[a] | before[a+1] << 8
    v1 = after[a] | after[a+1] << 8
    if v0 != v1 and 0 < v0 < 1024 and 0 < v1 < 1024 and abs(v1 - v0) < 400:
        runs.append((LO + a, v0, v1))
log("changed coord-like words outside the kart blocks ($1000-$1800):")
for a, v0, v1 in runs:
    if a >= 0x1800:
        log("  $%04X: %4d -> %4d" % (a, v0, v1))
log("total changed coord-like words: %d (of which >= $1800: %d)"
    % (len(runs), sum(1 for a, _, _ in runs if a >= 0x1800)))
