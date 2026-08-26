"""Dump the game's OWN direction field ($7F4000) and the arctangent table
it is built from ($7F9000), so the port's field can be checked against it
cell by cell instead of trusted."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

lab = Lab(settle=60)
w = lab.b.wram
track = lab.w(0x0124) // 2 if lab.w(0x0124) < 0x100 else lab.w(0x0124)
log("track word $0124 =", hex(lab.w(0x0124)))
flow = bytes(w[0x14000:0x15000])      # $7F4000
sect = bytes(w[0x15000:0x16000])      # $7F5000
atan = bytes(w[0x19000:0x1A000])      # $7F9000
open(sys.argv[1] if len(sys.argv) > 1 else "/tmp/flow.bin", "wb").write(flow)
open((sys.argv[1] if len(sys.argv) > 1 else "/tmp/flow") + ".sect", "wb").write(sect)
open((sys.argv[1] if len(sys.argv) > 1 else "/tmp/flow") + ".atan", "wb").write(atan)
log("flow nonzero", sum(1 for b in flow if b), "sect !=7F",
    sum(1 for b in sect if (b & 0x7F) != 0x7F), "atan nonzero", sum(1 for b in atan if b))
# waypoints
wp = [(lab.w(0x0900 + i*2), lab.w(0x0A00 + i*2)) for i in range(40)]
log("waypoints[0:6]", wp[:6])
