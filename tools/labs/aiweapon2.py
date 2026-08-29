#!/usr/bin/env python3
"""Does anything ever appear NEXT TO an AI kart?  Long attract run, P1
held slow so the field stays around it; every frame, every $80 block in
$1800..$1FFF whose x/y (+$18/+$1C) moved by more than 64 or first landed
within 32 of an AI kart is printed with its first 16 words."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
FRAMES = int(os.environ.get("FRAMES", "4000"))
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w = L.w; wram = L.b.wram
BL = list(range(0x1800, 0x2000, 0x80))
def pos(b): return w(b+0x18), w(b+0x1C)
prev = {b: pos(b) for b in BL}
for f in range(FRAMES):
    # slow: throttle only every other frame
    L.flow(1)
    ks = [(q, w(0x1000+q*0x100+0x18), w(0x1000+q*0x100+0x1C)) for q in range(8)]
    for b in BL:
        p = pos(b)
        if p != prev[b] and abs(p[0]-prev[b][0]) + abs(p[1]-prev[b][1]) > 64:
            q = min(ks, key=lambda k: abs(k[1]-p[0]) + abs(k[2]-p[1]))
            d = abs(q[1]-p[0]) + abs(q[2]-p[1])
            log("f%d $%04X (%d,%d)->(%d,%d) nearest kart %d d=%d | %s" % (f, b, *prev[b], *p, q[0], d,
                ' '.join("%04X" % w(b+i) for i in range(0, 32, 2))))
        prev[b] = p
    if f % 500 == 0: log("f%d P1 %s rank %d speed %d" % (f, L.pos(), w(0x10E6)//2, L.speed()))
log("done")
