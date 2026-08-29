#!/usr/bin/env python3
"""The item slot's own tilemap cells through a whole item: the WRAM HUD map
($0C00 + row*64 + col*2, 32 cells a row - $81:B31C writes $0C26/$0C28/
$0C66/$0C68) at columns 16..23, rows -1..3, before a roulette, through
it, with the item held, and after it is used (A = pad low bit 7)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
L = Lab(settle=120, zero=(0x0E50, 0x0E51))
w, sw = L.w, L.sw
def cells():
    rows=[]
    for r in range(-1,4):
        base=0x0C00+r*64
        rows.append(' '.join("%04X" % w(base+c*2) for c in range(16,24)))
    return rows
def show(tag):
    log(tag + " $0D70=%04X $0D78=%04X" % (w(0x0D70), w(0x0D78)))
    for r,l in zip(range(-1,4),cells()): log("   row %2d: %s" % (r,l))
L.pace(600)
show("before")
sw(0x0D70, 0xA000); sw(0x0D78, 0xC1)
last=None
for f in range(400):
    L.frame(0x80)
    c=cells()
    if c!=last: show("f%d" % f); last=c
    if f>20 and not (w(0x0D70) & 0x2000): break
show("landed")
for f in range(3):
    L.frame(0x80, 0x80)     # A pressed: use it
    show("use+%d" % f)
last=None
for f in range(120):
    L.frame(0x80)
    c=cells()
    if c!=last: show("after+%d" % f); last=c
log("done")
