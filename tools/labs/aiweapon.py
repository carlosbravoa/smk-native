#!/usr/bin/env python3
"""What the AI karts throw, seen from the object blocks.

Run the attract race a long way and watch every $40-byte block in
$1800..$1FFF: a block whose +$12 word goes live (bit 15) or that goes
from all-zero to something is a spawn.  For each, print the frame, the
block, its first words, and the nearest kart with its distance - so the
AI's weapons find themselves before anyone reads bank $85 for them.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

FRAMES = int(os.environ.get("FRAMES", "2400"))
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, s16 = L.w, L.s16
wram = L.b.wram
LO, HI, BS = 0x1800, 0x2000, 0x40

def karts():
    return [(k, w(0x1000 + k*0x100 + 0x18), w(0x1000 + k*0x100 + 0x1C), w(0x1000 + k*0x100 + 0x10)) for k in range(8)]

def live(b):
    return (wram[b+0x12] | wram[b+0x13] << 8) & 0x8000

def nonzero(b):
    return any(wram[b:b+BS])

prev = {b: (live(b), nonzero(b)) for b in range(LO, HI, BS)}
log("start: live blocks: %s" % ' '.join("$%04X" % b for b in range(LO, HI, BS) if prev[b][0]))
for f in range(FRAMES):
    L.flow(1)
    for b in range(LO, HI, BS):
        cur = (live(b), nonzero(b))
        if cur != prev[b] and (cur[0] and not prev[b][0] or cur[1] and not prev[b][1]):
            x = wram[b+0x18] | wram[b+0x19] << 8; y = wram[b+0x1C] | wram[b+0x1D] << 8
            near = min(karts(), key=lambda k: abs(k[1]-x) + abs(k[2]-y))
            words = ' '.join("%04X" % (wram[b+i] | wram[b+i+1] << 8) for i in range(0, BS, 2))
            log("f%d block $%04X %s at (%d,%d) nearest kart %d at (%d,%d) d=%d $10=%04X\n    %s"
                % (f, b, "LIVE" if cur[0] and not prev[b][0] else "filled", x, y, near[0], near[1], near[2],
                   abs(near[1]-x)+abs(near[2]-y), near[3], words))
        prev[b] = cur
    if f % 600 == 0: log("f%d P1 at %s rank %d" % (f, L.pos(), w(0x10E6)//2))
log("done")
