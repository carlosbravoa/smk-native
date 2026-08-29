#!/usr/bin/env python3
"""The star's palette cycle, off CGRAM.  Fire the star ($E0 |= $2000) on
the running attract race and print, every frame, every OBJ palette word
that differs from the frame before the star - so the cycle's period and
its colours are the game's."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, sw = L.w, L.sw
L.pace(600)
def pal(): return [L.b.cgram[i] | L.b.cgram[i+1] << 8 for i in range(0x100, 0x200, 2)]
base = pal()
log("before: " + ' '.join("%d:%s" % (p, ' '.join("%04X" % v for v in base[p*16:p*16+16])) for p in range(8)))
if not os.environ.get("NOSTAR"): sw(P1 + 0xE0, w(P1 + 0xE0) | 0x2000)
for f in range(140):
    L.frame(0x80)
    cur = pal()
    diff = [(i, cur[i]) for i in range(128) if cur[i] != base[i]]
    log("f%d $E2=%04X $4E=%04X diff(%d): %s" % (f, w(P1+0xE2), w(P1+0x4E), len(diff),
        ' '.join("%d.%d=%04X" % (i // 16, i % 16, v) for i, v in diff[:24])))
log("done")
