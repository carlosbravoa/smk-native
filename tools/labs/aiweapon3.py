#!/usr/bin/env python3
"""The user's conditions for the AI's weapons: only against the player,
only when near, and only from lap 2.  So: attract race un-demoed, every
kart's lap byte ($C1 = $7F + laps) pushed to lap 2 at once, P1 driven
hard to sit in the pack, and every frame: object blocks whose position
jumps, projectile blocks going live, and any $E0/$E2 bit rising on an AI
kart - with the nearest kart and the player's distance to it."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
FRAMES = int(os.environ.get("FRAMES", "6000"))
LAP = int(os.environ.get("LAP", "2"))
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, sw = L.w, L.sw; wram = L.b.wram
for q in range(8):
    wram[0x1000 + q*0x100 + 0xC1] = 0x7F + LAP
log("lap bytes: " + ' '.join("%02X" % wram[0x1000+q*0x100+0xC1] for q in range(8)))
BL = list(range(0x1800, 0x1A00, 0x80)) + [0x1A00, 0x1A80]
def pos(b): return w(b+0x18), w(b+0x1C)
prev = {b: pos(b) for b in BL}
prevlive = {b: w(b+0x12) & 0x8000 for b in BL}
preve = [(w(0x1000+q*0x100+0xE0), w(0x1000+q*0x100+0xE2)) for q in range(8)]
for f in range(FRAMES):
    L.flow(1)
    ks = [(q, w(0x1000+q*0x100+0x18), w(0x1000+q*0x100+0x1C)) for q in range(8)]
    px, py = ks[0][1], ks[0][2]
    for b in BL:
        p = pos(b); live = w(b+0x12) & 0x8000
        if (p != prev[b] and abs(p[0]-prev[b][0]) + abs(p[1]-prev[b][1]) > 64) or (live and not prevlive[b]):
            q = min(ks, key=lambda k: abs(k[1]-p[0]) + abs(k[2]-p[1]))
            log("f%d $%04X %s (%d,%d)->(%d,%d) nearest kart %d d=%d, player d=%d | %s" % (
                f, b, "LIVE" if live and not prevlive[b] else "jump", *prev[b], *p, q[0],
                abs(q[1]-p[0]) + abs(q[2]-p[1]), abs(px-p[0]) + abs(py-p[1]),
                ' '.join("%04X" % w(b+i) for i in range(0, 32, 2))))
        prev[b] = p; prevlive[b] = live
    for q in range(1, 8):
        e0, e2 = w(0x1000+q*0x100+0xE0), w(0x1000+q*0x100+0xE2)
        r0, r2 = e0 & ~preve[q][0], e2 & ~preve[q][1]
        if r0 or (r2 & ~0x8000):
            log("f%d kart %d E0 +%04X E2 +%04X  player d=%d" % (f, q, r0, r2, abs(px-ks[q][1]) + abs(py-ks[q][2])))
        preve[q] = (e0, e2)
    if f % 500 == 0:
        near = sorted(abs(px-k[1]) + abs(py-k[2]) for k in ks[1:])[:3]
        log("f%d P1 %s rank %d lap %02X nearest AI d=%s" % (f, L.pos(), w(0x10E6)//2, wram[0x10C1], near))
log("done")
