#!/usr/bin/env python3
"""The star's palette cycle, off OAM: after $E0 |= $2000 on P1, the
palette bits of every 16x16 sprite sitting where P1's kart is drawn
(x 96..160, y 56..112 in the top half), frame by frame."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, sw = L.w, L.sw
L.pace(600)
def kart_sprites():
    out=[]; o=L.b.oam
    for k in range(128):
        y=o[k*4+1]
        if y in (0,0xF0,0xE0): continue
        x,t,a=o[k*4],o[k*4+2],o[k*4+3]
        hi=o[512+(k>>2)]; big=(hi>>((k&3)*2+1))&1; x|=((hi>>((k&3)*2))&1)<<8
        t|=(a&1)<<8
        if big and 96<=x<=160 and 56<=y<=112: out.append((k,x,y,t,(a>>1)&7))
    return out
log("before: %s" % kart_sprites())
sw(P1 + 0xE0, w(P1 + 0xE0) | 0x2000)
for f in range(96):
    L.frame(0x80)
    s = kart_sprites()
    log("f%d $E2=%04X pal=%s tiles=%s" % (f, w(P1+0xE2), ' '.join(str(p) for *_, p in s), ' '.join("%03X" % t for _,_,_,t,_ in s)))
log("done")
