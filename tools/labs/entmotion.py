"""S12: do the track's objects MOVE, and who moves them?

$84DC80 reads $0D2C, indexes the pointer table at $84DD15, and $84DC98
then rewrites every object's $18/$1C from a list chosen by a per-group
clock ($C0,x).  So the moving obstacles are not the static $85:C800 list
at all - which is why Ghost Valley decodes to zero entities and still
shows four on screen.  Watch the blocks over time and find out.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import log
from track_force import boot

track = int(sys.argv[1]) if len(sys.argv) > 1 else 16
r, b, c = boot(track)
w = b.wram
log("track $0124 = $%02X   $0D2C = $%04X   $1EE4 = $%04X"
    % (w[0x0124], w[0x0D2C] | w[0x0D2D] << 8, w[0x1EE4] | w[0x1EE5] << 8))

def snap(n=8):
    out = []
    for e in range(n):
        blk = 0x1800 + e * 0x40
        out.append((w[blk+0x18] | w[blk+0x19] << 8,
                    w[blk+0x1C] | w[blk+0x1D] << 8,
                    w[blk+0x1F] | w[blk+0x20] << 8,
                    w[blk+0xC0] | w[blk+0xC1] << 8))
    return out

prev = snap()
log("frame   " + "  ".join("ent%d (x,y,z,clk)" % e for e in range(4)))
for f in range(0, 240):
    c.run_frames_scanline(1)
    if f % 12 == 0:
        s = snap()
        log("f%-4d " % f + "  ".join("(%4d,%4d,%5d,%4d)" % t for t in s[:4]))
s = snap()
moved = sum(1 for a, bb in zip(prev, s) if a[:3] != bb[:3])
log("blocks whose x/y/z changed over 240 frames: %d of 8" % moved)
