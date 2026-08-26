"""The real start grid: read every kart's position the moment the race
begins, so the per-track record at [$0C] can be found by matching.

$819207 unpacks it: x = (w & $7F) * 8 + $12, y = ((w & $3F80) >> 4) + $14,
with $12/$14 either 4 or 10 ($8191DE / $8191F4).
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

L = Lab(settle=0)
w = L.b.wram
log("track $0124 = %d   $0C = $%04X%02X" % (w[0x0124] | w[0x0125] << 8,
    w[0x0C] | w[0x0D] << 8, w[0x0E]))
log("kart   x     y     packed if off=4      packed if off=10")
for k in range(8):
    b = 0x1000 + k * 0x100
    x = w[b+0x18] | w[b+0x19] << 8
    y = w[b+0x1C] | w[b+0x1D] << 8
    p4 = (((y - 4) // 8) << 7) | ((x - 4) // 8)
    pa = (((y - 10) // 8) << 7) | ((x - 10) // 8)
    log("  %d   %4d  %4d    $%04X               $%04X" % (k, x, y, p4 & 0xFFFF, pa & 0xFFFF))
