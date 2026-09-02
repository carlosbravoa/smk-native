"""Battery A0: the first 24 frames of a standing start, frame by frame.

The class/coins sweep and the grip curve came out identical between the ROM
and the port, but the launch differed by a couple of units (18 vs 20 at
frame 10).  A couple of units at 60 Hz is a real difference in where the
kart is a second later, so it gets its own trace rather than a shrug.
"""
from lab import Lab, log, P1

L = Lab()
for i in range(0xC0):
    L.b.wram[0x0B00 + i] = 0x40


def st(a):
    v = L.w(P1 + a)
    return v - 65536 if v > 32767 else v


L.sw(P1 + 0x18, 512); L.sw(P1 + 0x1A, 0)
L.sw(P1 + 0x1C, 512); L.sw(P1 + 0x1E, 0)
for a in (0xE8, 0xEA, 0xEC, 0xEE, 0xA8, 0xAA, 0xA6, 0xFA, 0xB2):
    L.sw(P1 + a, 0)
log("start: $AC %04X  $E2 %04X  $10 %04X  $B0 %04X  $D6 %d  $B4 %d"
    % (L.w(P1 + 0xAC), L.w(P1 + 0xE2), L.w(P1 + 0x10), L.w(P1 + 0xB0),
       st(0xD6), st(0xB4)))
log("   f  $EA(speed)  $E8(frac)  $EE(accel)  $EC(accel frac)  $AC  $A6")
for f in range(25):
    L.sw(P1 + 0x18, 512); L.sw(P1 + 0x1A, 0)
    L.sw(P1 + 0x1C, 512); L.sw(P1 + 0x1E, 0)
    L.frame(0x80)
    log("  %2d  %9d  %9d  %10d  %14d  %3X  %3X"
        % (f, st(0xEA), L.w(P1 + 0xE8), st(0xEE), L.w(P1 + 0xEC),
           L.w(P1 + 0xAC), L.w(P1 + 0xA6)))
