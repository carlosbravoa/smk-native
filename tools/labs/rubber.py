"""The rubber band: what moves $C8,x?

$80B074 picks an AI kart's target-speed row from the waypoint attribute
OFFSET BY $C8,x.  If the game moves $C8 according to race position, that
is the catch-up cheat and the pre-determined order in one field.  Watch
it for every kart, with position, and log who writes it.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

L = Lab(settle=120)
w = L.b.wram
writers = {}
orig = L.b.write
cpu = L.c
def wr(bank, addr, val):
    if ((bank & 0x7F) <= 0x3F or bank == 0x7E) and 0x1000 <= addr <= 0x17FF \
       and (addr & 0xFF) in (0xC8, 0xC9):
        key = (cpu.PB, cpu.PC)
        writers[key] = writers.get(key, 0) + 1
    orig(bank, addr, val)
L.b.write = wr

def row(tag):
    c8 = [w[0x1000 + k*0x100 + 0xC8] | w[0x1000 + k*0x100 + 0xC9] << 8 for k in range(8)]
    wp = [w[0x1000 + k*0x100 + 0xC0] for k in range(8)]
    ea = [L.s16(w[0x1000 + k*0x100 + 0xEA] | w[0x1000 + k*0x100 + 0xEB] << 8) for k in range(8)]
    log("%-6s $C8 %s" % (tag, " ".join("%04X" % v for v in c8)))
    log("       wp  %s" % " ".join("%4d" % v for v in wp))
    log("       spd %s" % " ".join("%4d" % v for v in ea))

row("start")
for f in range(1200):
    L.frame(0x80)
    if f % 400 == 399:
        row("f%d" % f)
log("")
log("writers of $C8 in any kart block:")
for (pb, pc), n in sorted(writers.items(), key=lambda kv: -kv[1])[:8]:
    log("  $%02X:%04X  x%d" % (pb, pc, n))
