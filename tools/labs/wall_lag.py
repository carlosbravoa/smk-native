"""What ENDS the post-crash deceleration?

$80A55B's state $16 sets $EE from the table at $80A590 indexed by the
velocity lag $A8, and exits the moment $A8 is zero.  $80A9F7 walks $A8
toward zero $40 a frame - far too slowly to explain the three frames the
capture showed.  So watch who actually writes $A8, with the PC.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

lab = Lab(settle=60)
w = lab.b.wram
solid = [t for t in range(256) if w[0x0B00 + t] & 0x80]
lab.pace(600)

writes = []
orig = lab.b.write
cpu = lab.c
watching = [False]
def wr(bank, addr, val):
    if watching[0] and ((bank & 0x7F) <= 0x3F or bank == 0x7E) and addr in (0x10A8, 0x10A9):
        writes.append((len(writes), cpu.PB, cpu.PC, addr, val))
    orig(bank, addr, val)
lab.b.write = wr

def cell_ahead(px):
    x, y = lab.pos()
    a = lab.heading() * 2 * math.pi / 65536
    fx = int(x + math.sin(a) * px) & 1023
    fy = int(y - math.cos(a) * px) & 1023
    return (((fy - 1) & 1023) >> 3) * 128 + ((fx & 1023) >> 3)

cell = cell_ahead(24)
for d in (-2, -1, 0, 1, 2):
    for e in (-256, -128, 0, 128, 256):
        w[0x10000 + ((cell + d + e) & 0x3FFF)] = solid[0]
watching[0] = True
for f in range(20):
    lab.frame(0x80)
    a8 = lab.w(P1 + 0xA8)
    log("f%-2d $A8=%04X $EA=%04X $EE=%04X $AC=%04X  writes this frame: %s"
        % (f, a8, lab.w(P1 + 0xEA), lab.w(P1 + 0xEE), lab.w(P1 + 0xAC),
           " ".join("$%02X:%04X=%02X" % (b, pc, v) for _, b, pc, ad, v in writes if ad == 0x10A8)))
    writes.clear()
