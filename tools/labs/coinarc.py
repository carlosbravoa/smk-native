#!/usr/bin/env python3
"""The spilled coin's path ON SCREEN, from a real bump.

The user's test: "start any race, accelerate as soon as the countdown ends
and drive straight: you will hit the kart in front of you."  Done here by
putting P1 just behind the kart ahead at speed with B held; the game does
the bump.  Then every frame: the coin count, and every OAM sprite showing
a coin tile ($86 / $A2 / $60, palette 6 - NOTES 183) with its screen
position.  The kart's own sprite sits at a fixed screen spot, so the
coin's screen path IS the thing the port has to reproduce.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
L = Lab(settle=60, zero=(0x0E50, 0x0E51))
w, sw, s16 = L.w, L.sw, L.s16
for _ in range(200):
    L.flow(1)
    if L.speed() > 500: break
# the kart ahead: the nearest other kart in front along our heading
def coins(): return w(0x0E00)
def spr():
    out=[]; o=L.b.oam
    for k in range(128):
        y=o[k*4+1]
        if y in (0,0xF0,0xE0): continue
        x,t,a=o[k*4],o[k*4+2],o[k*4+3]
        hi=o[512+(k>>2)]; big=(hi>>((k&3)*2+1))&1; x|=((hi>>((k&3)*2))&1)<<8
        t|=(a&1)<<8
        if t in (0x86,0xA2,0x60) and ((a>>1)&7)==6 and x<256 and y<224: out.append((k,x,y,t))
    return out
best=None
for kb in range(0x1100,0x1800,0x100):
    dx=s16(w(kb+0x18)-w(P1+0x18)); dy=s16(w(kb+0x1C)-w(P1+0x1C)); d=dx*dx+dy*dy
    if best is None or d<best[0]: best=(d,kb)
kb=best[1]
log("nearest kart $%04X at (%d,%d); P1 at (%d,%d) speed %d coins %d" % (kb,w(kb+0x18),w(kb+0x1C),w(P1+0x18),w(P1+0x1C),L.speed(),coins()))
# park P1 12 px behind it, same heading, faster
sw(P1+0x18, w(kb+0x18)); sw(P1+0x1C, w(kb+0x1C)+12)
sw(P1+0xA4, w(kb+0xA4)); sw(P1+0xA2, w(kb+0xA4)); sw(P1+0x2A, w(kb+0xA4)); sw(P1+0xA8,0)
sw(P1+0xEA, min(w(kb+0xEA)+300, 900))
log("f  coins  kart(x,y,spd)  $10   | coin sprites (slot x,y tile)")
c0=coins(); hit=None
for f in range(150):
    L.frame(0x80)
    c=coins(); s=spr()
    if hit is None and c<c0: hit=f; log("  BUMP at f%d: coins %d -> %d" % (f,c0,c))
    if hit is not None and f-hit<=60 or s:
        log(" %3d %5d  (%4d,%4d,%4d) $%04X | %s" % (f,c,w(P1+0x18),w(P1+0x1C),L.speed(),w(P1+0x10)," ".join("s%d(%d,%d $%02X)"%(k,x,y,t) for k,x,y,t in s)))
    if hit is not None and f-hit>60: break
if hit is None: log("no bump in 150 frames")
