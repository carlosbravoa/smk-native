"""At what HEIGHT does a Thwomp stop being solid?

    tools/labs/thwomp_solid.py <cup> <course> <track>     e.g. 0 3 17

src/ai.c passes a kart under a mover above half the parked height, and
says so: "the height at which it stops touching is ours - the measurement
(NOTES 152) gives the motion, not the hit box."  The user pays for that
guess: "when you drive under a thwomp and that thwomp has just lifted up,
the system makes you crash/bounce against nothing... This does not happen
when the thwomp is super high, but happens while it is just going up,
when the original game lets you pass."

The routine that decides it is not in the decoded set - $80ADA0-style
static reading found nothing, because the object blocks are only ever
reached through an index register, so there is no absolute address to
search for.  So measure the behaviour instead of reading the code: the
object's height cycles on its own, the kart is placed on top of it every
frame with no speed, and the only thing that varies is z.  Whether the
game calls that a touch is read off $10 and off the shove it applies.

Built on movers.py, which is what got a capture this far (NOTES 152):
reach the track, run the countdown out, and complete a LAP - Thwomps are
inert on lap one, and every capture that skipped this found nothing.
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools")
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

cup    = int(sys.argv[1]) if len(sys.argv) > 1 else 0
course = int(sys.argv[2]) if len(sys.argv) > 2 else 3
want   = int(sys.argv[3]) if len(sys.argv) > 3 else 17
FRAMES = int(sys.argv[4]) if len(sys.argv) > 4 else 400

r = Rom.load("/home/carlos/extended/devel/games/mariokart/rom/smk_usa.sfc")
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return cup
        if addr == 0x0152: return course
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0

t0 = time.time()
while time.time() - t0 < 1200:
    c.run_frames_scanline(10)
    if b.wram[0x0124] == want and b.wram[0x36] // 2 in (1, 6):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10:
            break
w = b.wram
def _rw(a): return w[a] | w[a+1] << 8
# Run the countdown out and get the kart moving before capturing anything.
# Without this the whole capture happens on the starting grid, the scripts
# never tick, and every byte reads "unchanged" - which is a void run, not a
# finding (the first Rainbow Road attempt died exactly this way).
for _ in range(900):
    b.reg_reads[0x4219] = 0x80
    c.run_frames_scanline(1)
    if _rw(0x1000 + 0xEA) > 100:
        break
print("countdown done, player speed %d" % _rw(0x1000 + 0xEA), flush=True)

# THWOMPS ARE INERT ON LAP ONE.  The user: "thwomps in all the tracks
# start first lap up, then after finishing the first lap, they get
# activated."  Every capture before this one recorded lap 1 and duly found
# nothing moving.  So drive the flow field until the progress word $C0
# shows a completed lap - it starts at lap byte $7F, the first crossing
# takes it to $80 (entering lap 1) and the next to $81 (NOTES 148).
import time as _t
_t0 = _t.time()
while _t.time() - _t0 < 1500:
    x, y = _rw(0x1000+0x18), _rw(0x1000+0x1C)
    cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
    d = (( w[0x14000 + cell] << 8) - _rw(0x1000+0xA4)) & 0xFFFF
    if d > 32768: d -= 65536
    b.reg_reads[0x4219] = 0x80 | (0x02 if d < -0x300 else 0x01 if d > 0x300 else 0)
    c.run_frames_scanline(1)
    if (_rw(0x1000 + 0xC0) >> 8) >= 0x81:
        break
print("lap word $C0 = $%04X after the drive (>= $8100 means a lap is done)"
      % _rw(0x1000 + 0xC0), flush=True)
def rw(a): return w[a] | w[a+1] << 8
def s16(v): return v - 65536 if v > 32767 else v
def ww(a, v):
    w[a] = v & 0xFF; w[a+1] = (v >> 8) & 0xFF

print("track $%02X  $0D2C=%d" % (w[0x0124], rw(0x0D2C)), flush=True)

# ---- the question ----------------------------------------------------
# At what HEIGHT does a Thwomp stop being solid?
#
# src/ai.c skips collision above half the parked height:
#     if (smk_mover_z(crs, i) > SMK_MOVER_PARK / 2) continue;
# and the comment says so plainly - "the height at which it stops touching
# is ours".  The user pays for that guess: "when you drive under a thwomp
# and that thwomp has just lifted up, the system makes you crash/bounce
# against nothing... This does not happen when the thwomp is super high,
# but happens while it is just going up, when the original lets you pass."
#
# So sweep it.  The object's height cycles on its own (NOTES 152: fall,
# 135 frames down, then +64 a frame up), and the kart is PUT on top of the
# object every frame with no speed, so the only thing that varies is z.
# Whether the game calls that a touch is then read off $10 and the speed.
#
# The kart is teleported rather than driven because driving cannot hold a
# kart inside an object across a whole cycle - and what is being measured
# here is a threshold, not a trajectory, so the sprite-rig warning of
# NOTES 076 does not apply: there is nothing to associate or project.
BLK = 0x1800
prev10 = rw(0x1000 + 0x10)
print("  f      z  obj(x,y)   $10   d$10   spd  kart(x,y)  hazard drive", flush=True)
rows = []
for f in range(int(os.environ.get("SWEEP", "1400"))):
    ox, oy, oz = rw(BLK + 0x18), rw(BLK + 0x1C), s16(rw(BLK + 0x1F))
    # Put the kart INSIDE the object, but leave it DRIVING.  The first
    # attempt also forced speed and velocity to zero, and nothing ever
    # fired - not even at z = 0, on the floor.  A response gated on
    # CLOSING VELOCITY (which is what our own port does: `if (dot < 0)`)
    # cannot fire for a kart that is not moving, so a still kart tests
    # nothing.  Hold the throttle and let it keep its own velocity.
    ww(0x1000 + 0x18, ox)
    ww(0x1000 + 0x1C, oy)
    b.reg_reads[0x4219] = 0x80     # B held
    c.run_frames_scanline(1)
    f10 = rw(0x1000 + 0x10)
    spd = s16(rw(0x1000 + 0xEA))
    kx, ky = rw(0x1000 + 0x18), rw(0x1000 + 0x1C)
    haz, drv = rw(0x1000 + 0xA0), rw(0x1000 + 0xEE)
    rows.append((f, oz, f10, spd, kx - ox, ky - oy, haz, drv))
    if f % 1 == 0 and (f10 != prev10 or f < 5 or f % 50 == 0):
        print("  %4d %6d (%4d,%4d) $%04X $%04X %5d (%+4d,%+4d) haz $%04X drv $%04X"
              % (f, oz, ox, oy, f10, f10 ^ prev10, spd, kx - ox, ky - oy, haz, drv),
              flush=True)
    prev10 = f10

# which bit of $10, if any, tracks the height
import collections
print("\n  bit of $10 vs height - a bit that is the collision will be set at"
      " low z and clear at high z:", flush=True)
for bit in range(16):
    on = [r[1] for r in rows if r[2] & (1 << bit)]
    off = [r[1] for r in rows if not r[2] & (1 << bit)]
    if not on or not off: continue
    print("    bit %2d ($%04X): set for z %d..%d (%d frames), clear for z %d..%d (%d)"
          % (bit, 1 << bit, min(on), max(on), len(on), min(off), max(off), len(off)),
          flush=True)
# and the shove
push = [(r[1], r[4], r[5]) for r in rows if r[4] or r[5]]
if push:
    zs = sorted(z for z, _, _ in push)
    print("\n  the kart was SHOVED on %d frames, at heights %d..%d"
          % (len(push), zs[0], zs[-1]), flush=True)
    print("  the highest z that still shoved: %d  (parked is 4096)" % zs[-1], flush=True)
else:
    print("\n  the kart was never shoved - the detector is wrong, not the game",
          flush=True)

print("\n  speed, hazard and drive state by height band - the kart is driving"
      " into the object at every one:", flush=True)
bands = {}
for r in rows:
    b_ = (r[1] // 512) * 512
    bands.setdefault(b_, []).append(r)
for b_ in sorted(bands):
    v = bands[b_]
    sp = sorted(x[3] for x in v)
    hz = sorted(set(x[6] for x in v))
    dv = sorted(set(x[7] for x in v))
    print("    z %5d..%5d  n=%4d  speed med %5d min %5d  hazard %s  drive %s"
          % (b_, b_ + 511, len(v), sp[len(sp)//2], sp[0],
             " ".join("$%04X" % h for h in hz[:4]),
             " ".join("$%04X" % d for d in dv[:4])), flush=True)
