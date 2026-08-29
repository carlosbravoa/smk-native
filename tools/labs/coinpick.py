#!/usr/bin/env python3
"""The PICKED-UP coin's path on screen.  Drive the attract race's own
track by the flow field (it runs over the coin rows), and from the frame
$0E00 goes UP log every OAM sprite wearing a coin tile ($86/$A2/$60,
palette 6 - NOTES 183) for 60 frames, plus any sprite with another tile
that appears at the same spot (the pickup may use its own frames)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(settle=60, zero=(0x0E50, 0x0E51))
w = L.w
def coins(): return w(0x0E00)
def spr():
    out=[]; o=L.b.oam
    for k in range(128):
        y=o[k*4+1]
        if y in (0,0xF0,0xE0): continue
        x,t,a=o[k*4],o[k*4+2],o[k*4+3]
        hi=o[512+(k>>2)]; big=(hi>>((k&3)*2+1))&1; x|=((hi>>((k&3)*2))&1)<<8
        t|=(a&1)<<8
        if x<256 and y<224: out.append((k,x,y,t,(a>>1)&7,big))
    return out
c0=coins(); got=0
for f in range(4000):
    L.flow(1)
    c=coins()
    if c>c0:
        log("PICKUP at f%d: coins %d -> %d, kart at %s speed %d" % (f,c0,c,L.pos(),L.speed()))
        before=set((s[3],s[4]) for s in spr())
        for g in range(60):
            s=[x for x in spr() if x[3] in (0x86,0xA2,0x60) or (x[3],x[4]) not in before]
            log("  +%2d coins %d | %s" % (g,coins()," ".join("s%d(%d,%d $%03X p%d %s)"%(k,x,y,t,p,'L' if b else 's') for k,x,y,t,p,b in s)))
            L.flow(1)
        got+=1
        if got>=2: break
    c0=coins()
log("done, %d pickups" % got)
