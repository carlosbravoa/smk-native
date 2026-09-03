#!/usr/bin/env python3
"""Does the camera turn a FULL circle with the feather, or half of one?

NOTES 196 measured the camera azimuth as $A4 + $C0 + $AA/2 through the
object TUMBLE (state $1A).  The port applies the same half to the
feather's own flight (state $18), and the user says the camera does not
go round: "when jumping with a feather, the camera doesn't do a 360.
Only the player."  So: poke a READY feather, press A, and log $94 against
$A4 and $AA every frame of the flight.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log
P1 = 0x1000
L = Lab(zero=(0x0E50, 0x0E51))
L.reach_race()
L.pace(500)
w, s16 = L.w, L.s16
L.b.wram[0x0D70] = 0x01; L.b.wram[0x0D71] = 0xC0     # READY feather
L.frame(0x80, 0x80)                                   # A: use it
log("pressed; item word %04X  state $A6 %02X" % (w(0x0D70), L.b.wram[P1 + 0xA6]))
log("  f  $94 cam  $A4 head  $AA lag   $2A pose  $A6  z     cam-head  (cam-head)/lag")
for f in range(46):
    cam, head, lag = w(0x94), w(P1 + 0xA4), s16(w(P1 + 0xAA))
    d = (cam - head) & 0xFFFF
    if d > 32768: d -= 65536
    log("  %2d  %6d  %6d  %6d  %6d   %02X  %5d  %+6d   %s"
        % (f, cam, head, lag, w(P1 + 0x2A), L.b.wram[P1 + 0xA6], w(P1 + 0x1F),
           d, ("%.3f" % ((d - 0xC0) / lag)) if lag else "-"))
    L.frame(0x80)
