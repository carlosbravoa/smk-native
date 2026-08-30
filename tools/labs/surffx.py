#!/usr/bin/env python3
"""What each SURFACE does to the picture: the kart sprite's shake and the
effect sprites the game spawns, class by class.  The road is made one
class at a time (surface_fill, as the grip and cap sweeps did), the kart
driven on it with B held, and every frame: the kart's four sprite rows
(tiles $180-$1A3), every other sprite within 48 px of the kart (tile,
attr, x, y), the speed, and a few kart words that might carry the shake."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
CLASSES = [int(x, 16) for x in os.environ.get("CLASSES", "40 42 44 46 48 4A 4C 4E 50 52 54 56 58 5A 5C 5E 20 22 24").split()]
FRAMES = int(os.environ.get("FRAMES", "80"))
L = Lab(settle=90, zero=(0x0E50, 0x0E51))
w, s16 = L.w, L.s16
snap = L.surface_snapshot()
def sprites():
    out=[]; o=L.b.oam
    for k in range(128):
        y=o[k*4+1]
        if y in (0,0xF0,0xE0): continue
        x,t,a=o[k*4],o[k*4+2],o[k*4+3]
        hi=o[512+(k>>2)]; big=(hi>>((k&3)*2+1))&1; x|=((hi>>((k&3)*2))&1)<<8
        t|=(a&1)<<8
        out.append((k,x,y,t,a,big))
    return out
L.pace(600)
print("cls,f,spd,kart_rows,others,w2C,w2E,w30,w32,wEE,w0E5E")
for cls in CLASSES:
    L.surface_fill(snap, cls)
    for f in range(FRAMES):
        L.flow(1)
        sp = sprites()
        karts = [(x,y) for k,x,y,t,a,big in sp if 0x180 <= t <= 0x1A3]
        kx = min((x for x,y in karts), default=112); ky = min((y for x,y in karts), default=70)
        others = [(t,a,x-kx,y-ky) for k,x,y,t,a,big in sp if not (0x180 <= t <= 0x1A3) and abs(x-kx) < 48 and abs(y-ky) < 48]
        print("%02X,%d,%d,%s,%s,%d,%d,%d,%d,%d,%d" % (cls, f, L.speed(),
              ' '.join("%d:%d" % p for p in karts), ' '.join("t%03X/a%02X@%d,%d" % o for o in others),
              s16(w(P1+0x2C)), s16(w(P1+0x2E)), s16(w(P1+0x30)), s16(w(P1+0x32)), s16(w(P1+0xEE)), w(0x0E5E)), flush=True)
    L.surface_restore(snap)
    for _ in range(30): L.flow(1)          # settle back on the road
log("done")
