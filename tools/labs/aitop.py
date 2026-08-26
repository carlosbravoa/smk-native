"""Do AI karts differ by CHARACTER at all?

They carry no per-player block ($B4 = 0), so they cannot be using the
character tables the player uses.  But the ROM might still pick their
speed per character somewhere else.  Watch every kart's top speed over a
long stretch and see whether the numbers cluster into the character pairs
(944 / 912 / 880 / 864 at 100cc) or land on one shared value.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

L = Lab(settle=120)
w = L.b.wram
top = [0] * 8
for f in range(1500):
    L.frame(0x80)
    for k in range(8):
        v = L.s16(w[0x1000 + k*0x100 + 0xEA] | w[0x1000 + k*0x100 + 0xEB] << 8)
        if 0 < v < 4000 and v > top[k]:
            top[k] = v
log("engine class word $0126 = %d" % (w[0x0126] | w[0x0127] << 8))
log("kart  top speed seen   $B4")
for k in range(8):
    b4 = w[0x1000 + k*0x100 + 0xB4] | w[0x1000 + k*0x100 + 0xB5] << 8
    log("  %d   %5d           $%04X" % (k, top[k], b4))
log("")
log("the player tables at 100cc: Bowser/DK 944, Mario/Luigi 912, Peach/Yoshi 880, Koopa/Toad 864")
log("distinct AI tops (karts 2-7): %s" % sorted({top[k] for k in range(2, 8)}))
