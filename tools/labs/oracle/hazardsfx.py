"""Which sound ids each hazard class queues, and when: fill, drive on, log the
queue ($0E6C.., index $0E6A) and $A0/$CA per frame."""
import sys
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools/labs")
from lab import Lab, log
lab = Lab(settle=120)
snap = lab.surface_snapshot()
w = lab.b.wram
for cls in (0x22, 0x24, 0x26, 0x20, 0x28):
    lab.surface_restore(snap)
    lab.pace(400)
    lab.surface_fill(snap, cls)
    events = []; lastq = None; prev_a0 = None
    for f in range(420):
        lab.frame(0x80, 0)
        q = bytes(w[0x0E6C:0x0E80]); a0 = w[0x10A0]
        if q != lastq:
            new = [("%02X" % b) for b in q if b not in (0, 0x7E)]
            events.append((f, "queue", new[:4], "A0=%d" % a0, "CA=%d" % lab.w(0x10CA)))
            lastq = q
        if a0 != prev_a0:
            events.append((f, "A0", a0, "CA=%d" % lab.w(0x10CA)))
            prev_a0 = a0
    log("class $%02X:" % cls, events[:24])
    lab.surface_restore(snap)
    for _ in range(600): lab.frame(0x80, 0)
