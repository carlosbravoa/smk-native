"""Are the obstacles RE-SPAWNED as the player advances round the lap?

$84DBFF reads $C0,x with X = the KART base - the player's waypoint, not a
timer - counts how many thresholds in ($16) it has passed to get a lap
SEGMENT, and when that segment changes ($0D34,y) it respawns the whole
object set from $85:C800 at the offset $84DAC5,x.  $84DC80/$84DC98 index
the motion path by the same waypoint.  So: drive, and watch the segment,
the waypoint and every object block's x/y together.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

L = Lab(settle=60)
w = L.b.wram
log("track $0124=$%02X  $0D2C=$%02X  $1EE4=$%04X"
    % (w[0x0124], w[0x0D2C], w[0x1EE4] | w[0x1EE5] << 8))

def blocks():
    out = []
    for e in range(8):
        b = 0x1800 + e * 0x40
        out.append((w[b+0x18] | w[b+0x19] << 8, w[b+0x1C] | w[b+0x1D] << 8))
    return out

def seg():
    return w[0x0D34] | w[0x0D35] << 8

log("frame  wp seg  objects")
prev = None
for f in range(0, 900):
    L.flow(1)
    b = blocks()
    if b != prev or f % 150 == 0:
        log("f%-4d %3d %3d  %s" % (f, w[P1 + 0xC0], seg(),
            " ".join("(%d,%d)" % t for t in b)))
        prev = b
