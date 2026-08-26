"""S6: measure the wall response frame by frame.

Get the player up to speed on the game's own flow field, then paint a
SOLID tile into the cell it is about to enter and log the kart block while
the game reacts.  Everything the wall code touches is in the log:
$22/$24 (velocity), $EA (speed), $10 (flags), $52/$56 (the bounce state
$80FBAB sets), $5A/$5C (the in-wall counters), $26/$1F (z) and $42.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

lab = Lab(settle=60)
EXTRA = int(sys.argv[1]) if len(sys.argv) > 1 else 0
w = lab.b.wram

# a tile whose class has bit 7 - a real wall in this theme
solid = [t for t in range(256) if w[0x0B00 + t] & 0x80]
log("solid tile ids:", solid[:8])
if not lab.pace(600):
    log("could not reach speed:", lab.speed())
lab.flow(EXTRA)
log("speed", lab.speed(), "pos", lab.pos(), "heading", hex(lab.heading()))

def cell_ahead(px):
    x, y = lab.pos()
    a = lab.heading() * 2 * math.pi / 65536
    fx = int(x + math.sin(a) * px) & 1023
    fy = int(y - math.cos(a) * px) & 1023
    return (((fy - 1) & 1023) >> 3) * 128 + ((fx & 1023) >> 3), fx, fy

FIELDS = [("22",0x22),("24",0x24),("EA",0xEA),("EE",0xEE),("10",0x10),("5C",0x5C),
          ("A2",0xA2),("A4",0xA4),("A8",0xA8),("AA",0xAA),("A6",0xA6),("AC",0xAC),
          ("B2",0xB2),("FA",0xFA),("AE",0xAE),("C2",0xC2)]
def row(tag):
    return tag + " " + " ".join("%s=%04X" % (n, lab.w(P1 + o)) for n, o in FIELDS)

# paint a wall three cells ahead and drive into it
cell, fx, fy = cell_ahead(24)
was = w[0x10000 + cell]
for d in (-2, -1, 0, 1, 2):                # a short barrier, not one cell
    for e in (-256, -128, 0, 128, 256):
        w[0x10000 + ((cell + d + e) & 0x3FFF)] = solid[0]
log("painted wall at cell", cell, "target", (fx, fy), "tile was", hex(was))
log(row("f-1"))
for f in range(34):
    lab.frame(0x80)                         # throttle held, no steering
    log(row("f%-3d" % f))
