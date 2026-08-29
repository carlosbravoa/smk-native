#!/usr/bin/env python3
"""Which CGRAM entries the per-frame 44-byte palette DMA lands on, and how
they move: the game double-buffers $7E:3B80/$7E:3BC0 and $80:B824/B835
rewrite them every frame (hudpal2).  If that DMA targets the BG3 block at
64+, the item palettes are ANIMATED and one CGRAM dump is one phase."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(settle=30, zero=(0x0E50, 0x0E51))
b = L.b
frame=[0]; hits=[]
orig=b._ppu_write
def hook(reg,val):
    if reg == 0x2122:
        a = b.cgadd & 0x1FF
        if 128 <= a < 200: hits.append((frame[0], a, val))
    return orig(reg,val)
b._ppu_write = hook
L.sw(0x0D70, 0xC004); L.sw(0x0D78, 0)       # a green shell, READY
for f in range(64):
    L.flow(1); frame[0]+=1
lo = min(h[1] for h in hits) if hits else -1; hi = max(h[1] for h in hits) if hits else -1
log("CGRAM bytes written per frame: %d..%d  (entries %d..%d)" % (lo, hi, lo//2, hi//2))
# colours 80..95 per frame, from the hook stream
import collections
per = collections.defaultdict(dict)
for f,a,v in hits: per[f][a]=v
for f in sorted(per)[:20]:
    row=per[f]
    def c(i):
        lo_=row.get(i*2); hi_=row.get(i*2+1)
        return "%04X" % (lo_ | hi_<<8) if lo_ is not None and hi_ is not None else "----"
    log("  f%2d  pal4 %s %s %s %s  pal6 %s %s %s %s  pal7 %s %s %s %s" % (f, *(c(i) for i in range(80,84)), *(c(i) for i in range(88,92)), *(c(i) for i in range(92,96))))
