"""Lakitu fishing a fallen kart out: what is on screen, and where.

The rescue's STATE MACHINE is already ported (NOTES 113/124) - $A0 walks
6 -> $0C -> $0E, the kart is lifted, carried x then y at 2 px a frame,
and lowered $80 a frame.  What the port has never had is Lakitu himself.

Drop P1 into a hole on Ghost Valley and record the OAM for the whole
sequence, with the kart's own state beside it so the sprites can be tied
to the phase they belong to.

    python3 tools/labs/lakitu_rescue.py [track]
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import log
from track_force import boot

track = int(sys.argv[1]) if len(sys.argv) > 1 else 16
r, b, c = boot(track)
log("track $%02X" % b.wram[0x0124])

def w(a): return b.wram[a] | b.wram[a + 1] << 8
def sw(a, v): b.wram[a] = v & 0xFF; b.wram[a + 1] = (v >> 8) & 0xFF
def s16(v): return v - 65536 if v > 32767 else v
P1 = 0x1000

# let the countdown finish and get the field moving
for _ in range(700):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if s16(w(0x0146)) == 0 and b.wram[0x3A] == 6: break
for _ in range(120):
    b.reg_reads[0x4219] = 0x80
    c.run_frames_scanline(1)

# find a hole: a cell whose surface class is the FALL ($24/$26)
surf = b.wram[0x0B00:0x0BC0]
hole = None
for cell in range(0x4000):
    t = b.wram[0x10000 + cell] if False else None
    break
# the live tilemap is at $7F:0000 in this build's WRAM image; walk the
# kart's own neighbourhood instead and look for the class the hard way
log("kart at (%d,%d)" % (w(P1+0x18), w(P1+0x1C)))

# Ghost Valley's void is off the edge of the road; step the kart sideways
# until the game itself decides it has fallen.
start = w(P1 + 0x18)
for step in range(1, 60):
    sw(P1 + 0x18, (start + step * 8) & 0xFFFF)
    b.reg_reads[0x4219] = 0x80
    c.run_frames_scanline(1)
    if w(P1 + 0xA0):
        log("fell at x offset %d, $A0 = $%02X" % (step * 8, w(P1 + 0xA0)))
        break
else:
    log("never fell; $A0 = $%02X" % w(P1 + 0xA0))

def sprites():
    out = []
    for k in range(128):
        y = b.oam[k*4+1]
        if y in (0, 0xF0, 0xE0): continue
        hi = b.oam[0x200 + (k >> 2)]; sh = (hi >> ((k & 3) * 2)) & 3
        out.append((k, b.oam[k*4] | ((sh & 1) << 8), y,
                    b.oam[k*4+2] | ((b.oam[k*4+3] & 1) << 8),
                    b.oam[k*4+3], (sh >> 1) & 1))
    return out

log(" f  $A0  z($1E) kart(x,y) | sprites that are not karts")
prev = None
for f in range(260):
    odd = [s for s in sprites()
           if not (0xC0 <= (s[3] & 0xFF) <= 0xDF) and (s[3] & 0x1FF) >= 0x40
           and (s[3] & 0x1FF) < 0x140]
    key = tuple(sorted((s[3], s[4]) for s in odd))
    if key != prev or f % 30 == 0:
        log("%3d  $%02X  %5d (%4d,%4d) | %s"
            % (f, w(P1+0xA0), s16(w(P1+0x1E)), w(P1+0x18), w(P1+0x1C),
               ' '.join("t$%03X@%d,%d%s" % (s[3], s[1], s[2],
                                            "L" if s[5] else "")
                        for s in odd[:9])))
        prev = key
    b.reg_reads[0x4219] = 0x80
    c.run_frames_scanline(1)
