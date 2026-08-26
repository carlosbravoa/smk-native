"""Who writes an object block's +$06, and what is it really?

A write watch on $7E:1806 with the PC logged - the Python-oracle version
of the trick that cracked the breakable blocks.
"""
import sys, os, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log, P1

L = Lab(settle=60)
hits = {}
orig = L.b.write
cpu = L.c
def wr(bank, addr, val):
    if (bank & 0x7F) <= 0x3F or bank == 0x7E:
        if addr in (0x1806, 0x1807, 0x1886, 0x1887):
            key = (cpu.PB, cpu.PC)
            e = hits.setdefault(key, [0, []])
            e[0] += 1
            if len(e[1]) < 6:
                e[1].append((addr, val))
    orig(bank, addr, val)
L.b.write = wr
for _ in range(180):
    L.flow(1)
log("writers of $1806/$1886 (PC -> count, samples):")
for (pb, pc), (n, samp) in sorted(hits.items(), key=lambda kv: -kv[1][0]):
    log("  $%02X:%04X  x%-5d  %s" % (pb, pc, n,
        " ".join("$%04X=%04X" % s for s in samp[:4])))
