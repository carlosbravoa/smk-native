"""Which objects MOVE, on the tracks the user says have them?

Bowser Castle (Thwomps), Rainbow Road, Donut Plains (moles).  Force the
track, run a long stretch, and log every object slot's position - if any
slot's x/y changes while the player's waypoint does NOT, that is motion
rather than the path re-placement Ghost Valley does.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import log
from track_force import boot

track = int(sys.argv[1]) if len(sys.argv) > 1 else 3
r, b, c = boot(track)
w = b.wram
log("track $0124=%d  $0D28=%d  $0D2C=%d" % (w[0x0124], w[0x0D28], w[0x0D2C]))
def snap():
    return [(w[0x1800+i*0x80+0x18] | w[0x1800+i*0x80+0x19] << 8,
             w[0x1800+i*0x80+0x1C] | w[0x1800+i*0x80+0x1D] << 8,
             w[0x1800+i*0x80+0x1F] | w[0x1800+i*0x80+0x20] << 8) for i in range(4)]
prev, wp_prev, moved = snap(), w[0x10C0], 0
log("frame  wp   slots (x,y,z)")
for f in range(1200):
    c.run_frames_scanline(1)
    s, wp = snap(), w[0x10C0]
    if s != prev and wp == wp_prev:
        moved += 1
        if moved <= 12:
            log("f%-5d %3d  %s   <- moved with the waypoint STILL" %
                (f, wp, " ".join("(%d,%d,%d)" % t for t in s)))
    prev, wp_prev = s, wp
    if f % 300 == 299:
        log("f%-5d %3d  %s" % (f, wp, " ".join("(%d,%d,%d)" % t for t in s)))
log("frames where a slot moved without the waypoint changing: %d" % moved)
