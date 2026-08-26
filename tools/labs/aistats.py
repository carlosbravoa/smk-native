"""Do the ROM's AI karts have per-character stats, or share one set?

Each kart has $B4,x - the pointer to its per-player block - and $EA/$E8
its speed.  If the AI karts point at different blocks they have their own
tables; if they all point at one, their character does not change how they
drive and our shared-physics AI is already faithful.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

L = Lab(settle=120)
w = L.b.wram
log("kart   $B4 (block)  $EA speed  $C0 waypoint  character-ish $30..")
for k in range(8):
    base = 0x1000 + k * 0x100
    b4 = w[base+0xB4] | w[base+0xB5] << 8
    ea = L.s16(w[base+0xEA] | w[base+0xEB] << 8)
    c0 = w[base+0xC0]
    log("  %d    $%04X       %5d      %3d" % (k, b4, ea, c0))
blocks = sorted({w[0x1000 + k*0x100 + 0xB4] | w[0x1000 + k*0x100 + 0xB5] << 8 for k in range(8)})
log("distinct blocks in play: %s" % [hex(b) for b in blocks])
# and the top speed each block carries ($B4 block + $34 is the top per NOTES?)
for b in blocks:
    if b < 0x2000:
        row = " ".join("%04x" % (w[b + i] | w[b+i+1] << 8) for i in range(0, 24, 2))
        log("  block $%04X: %s" % (b, row))
