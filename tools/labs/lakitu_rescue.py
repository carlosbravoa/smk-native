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
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup = int(sys.argv[1]) if len(sys.argv) > 2 else 0
course = int(sys.argv[2]) if len(sys.argv) > 2 else 2   # cup 0 course 2 = GV1

# Reaching another track the NOTES 118 way: hook $0150/$0152 so mode entry
# computes $0124 AND the theme.  track_force's $0124 hook is the NOTES 059
# trap and leaves the race half set up - the kart came out at x = 65520.
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
_orig = b.read
def _rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return cup
        if addr == 0x0152: return course
    return _orig(bank, addr)
b.read = _rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
_t0 = time.time()
while time.time() - _t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and (b.wram[0x1018] or b.wram[0x1019]):
        if sum(1 for k in range(128)
               if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
log("track $%02X theme $%02X" % (b.wram[0x0124], b.wram[0x0126]))

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

log("kart at (%d,%d)" % (w(P1+0x18), w(P1+0x1C)))

# Ghost Valley's void is off the edge of the road, so walk the kart out
# of the road in both directions and let the GAME decide it has fallen -
# no reading of the surface table, no guessing which cell is a hole.
sx, sy = w(P1 + 0x18), w(P1 + 0x1C)
fell = False
for dx, dy in ((8, 0), (-8, 0), (0, 8), (0, -8)):
    for step in range(1, 40):
        sw(P1 + 0x18, (sx + dx * step) & 0xFFFF)
        sw(P1 + 0x1C, (sy + dy * step) & 0xFFFF)
        b.reg_reads[0x4219] = 0x80
        c.run_frames_scanline(1)
        if w(P1 + 0xA0):
            log("fell %d px along (%d,%d): $A0 = $%02X"
                % (step * 8, dx, dy, w(P1 + 0xA0)))
            fell = True
            break
    if fell: break
    sw(P1 + 0x18, sx); sw(P1 + 0x1C, sy)
if not fell:
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

log(" f  $A0  z($1E) kart(x,y) | sprites that are not karts || KART sprites (tile $180-$1A3): slot x,y")
prev = None
for f in range(260):
    odd = [s for s in sprites()
           if not (0xC0 <= (s[3] & 0xFF) <= 0xDF) and (s[3] & 0x1FF) >= 0x40
           and (s[3] & 0x1FF) < 0x140]
    key = tuple(sorted((s[3], s[4]) for s in odd))
    if key != prev or f % 30 == 0:
        ks=[(k,x,y) for k,x,y,t,a,big in sprites() if 0x180<=t<=0x1A3]
        log("KART f%d $%02X z %d | %s" % (f, w(P1+0xA0), s16(w(P1+0x1E)), ' '.join('s%d(%d,%d)'%v for v in ks)))
        log("%3d  $%02X  %5d (%4d,%4d) | %s"
            % (f, w(P1+0xA0), s16(w(P1+0x1E)), w(P1+0x18), w(P1+0x1C),
               ' '.join("t$%03X@%d,%d%s" % (s[3], s[1], s[2],
                                            "L" if s[5] else "")
                        for s in odd[:9])))
        prev = key
    b.reg_reads[0x4219] = 0x80
    c.run_frames_scanline(1)
