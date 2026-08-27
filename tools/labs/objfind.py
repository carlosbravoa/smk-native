"""Where does the GAME keep the object it is currently showing?

Our entity list for track 7 is decoded (NOTES 078).  Drive the kart round
with the flow field and, every so often, scan low WRAM for a word equal to
one of those x values with its y in the next word.  Whatever address keeps
matching is the live slot.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

ENT = [(268,92),(164,132),(188,52),(252,124),(508,636),(148,676),(260,740),(244,748)]
L = Lab(settle=60)
w = L.b.wram
def rd(a): return w[a] | w[a+1] << 8

seen = {}
for round_ in range(10):
    L.flow(80)
    hits = []
    for a in range(0x0000, 0x2000, 2):
        x, y = rd(a), rd(a + 2)
        for ex, ey in ENT:
            if x == ex and y == ey:
                hits.append((a, x, y))
    for a, x, y in hits:
        seen.setdefault(a, []).append((x, y))
    log("round %d: kart %s, %d matches %s"
        % (round_, L.pos(), len(hits),
           ", ".join("$%04X(%d,%d)" % h for h in hits[:6])))
log("\naddresses that matched more than once:")
for a, v in sorted(seen.items()):
    if len(v) > 1:
        log("  $%04X  %s" % (a, v[:6]))
