"""Where in the $1800 entity block are the coordinates?

Track 7's list gives entity world positions; find the byte offsets in the
live block that carry them, so a lab can drive the kart to a pipe.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

L = Lab(settle=60)
track = L.b.wram[0x0124]
log("track byte $0124 = %d" % track)
for e in range(4):
    b = 0x1800 + e * 0x40
    row = " ".join("%02X" % L.b.wram[b + i] for i in range(0x40))
    log("ent %d @$%04X: %s" % (e, b, row))
log("\nwords that look like world coords (0..1023):")
for e in range(4):
    b = 0x1800 + e * 0x40
    for i in range(0, 0x40, 2):
        v = L.b.wram[b + i] | L.b.wram[b + i + 1] << 8
        if 0 < v < 1024:
            log("  ent %d +$%02X = %d" % (e, i, v))
