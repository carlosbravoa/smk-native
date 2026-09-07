"""What each hazard class does to the player, forced: fill every driveable
tile with the class, drive on, log $A0/$CA/$EA/$1F per frame."""
import sys, os
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools/labs")
from lab import Lab, log
lab = Lab(settle=120)
snap = lab.surface_snapshot()
w = lab.b.wram
for cls in (0x22, 0x24, 0x26, 0x20, 0x28):
    lab.surface_restore(snap)
    lab.pace(400)
    lab.surface_fill(snap, cls)
    trace = []
    for f in range(400):
        lab.frame(0x80, 0)
        trace.append((w[0x10A0], lab.w(0x10CA), lab.w(0x10EA), w[0x101F], w[0x10AC]))
    # summarise: the sequence of $A0 values with the frame they began and $CA then
    seq = []; prev = None
    for i, (a0, ca, ea, z, ac) in enumerate(trace):
        if a0 != prev: seq.append((i, a0, ca, ea)); prev = a0
    log("class $%02X: A0 sequence (frame, A0, $CA, speed): %s" % (cls, seq[:10]))
    lab.surface_restore(snap)
    for _ in range(600): lab.frame(0x80, 0)     # let the rescue finish
