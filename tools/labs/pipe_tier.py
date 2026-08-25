"""Which art does the game pick for a pipe at each distance?

The entity block's +$02 is a sprite tile number and +$04 a draw routine;
a pipe is a PAIR of blocks.  Move the pair to a chosen distance straight
ahead of the kart and read back what the game selects.  That is the tier
ladder, measured, instead of inferred from the object sheet.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

L = Lab(settle=60)

def put(blk, x, y):
    L.b.wram[blk + 0x18] = x & 0xFF
    L.b.wram[blk + 0x19] = (x >> 8) & 0xFF
    L.b.wram[blk + 0x1C] = y & 0xFF
    L.b.wram[blk + 0x1D] = (y >> 8) & 0xFF

def wd(blk, off):
    return L.b.wram[blk + off] | L.b.wram[blk + off + 1] << 8

L.flow(120)
log("dist   blk0:+02 +04 +06 +0E   blk1:+02 +04 +06 +0E")
for dist in (320, 280, 240, 208, 176, 144, 128, 112, 96, 80, 64, 48, 32, 16):
    for _ in range(10):
        kx, ky = L.pos()
        h = L.heading() * 2 * math.pi / 65536
        ex, ey = int(kx + dist * math.sin(h)), int(ky - dist * math.cos(h))
        put(0x1800, ex, ey); put(0x1840, ex, ey)
        L.flow(1)
    log("%4d   $%04X $%04X $%04X $%04X   $%04X $%04X $%04X $%04X" % (
        dist, wd(0x1800, 2), wd(0x1800, 4), wd(0x1800, 6), wd(0x1800, 0x0E),
        wd(0x1840, 2), wd(0x1840, 4), wd(0x1840, 6), wd(0x1840, 0x0E)))
