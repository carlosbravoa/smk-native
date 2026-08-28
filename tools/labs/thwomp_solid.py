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

RUN THE CONTROL BEFORE BELIEVING A NEGATIVE.  The first two versions of
this lab parked the kart on the object's centre every frame and reported
no collision at any height, including z = 0 on the floor.  That reads
like a finding.  It was not: the same rig against a PIPE - where NOTES
072 measured a real crash - also reported nothing.  A rig that cannot
reproduce a known collision cannot tell you about an unknown one.  So the
pipe run is a positive control and it is not optional:

    cp thwomp_solid.py /tmp/pipe.py && python3 /tmp/pipe.py 0 0 14
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
# ---- how a hit is detected -------------------------------------------
# NOTES 072 measured the pipe crash directly: contact sets $10 bit $0002,
# the velocity reflects, the speed scales to ~53%, and a ~9-frame
# knockback follows with $AC = $16.  The first version of this lab watched
# $EE and called the kart's own travel a "shove", so it could not have
# seen a collision even if one happened - both are fixed here.
HIT_BIT = 0x0002
KNOCK   = 0x16

# ---- the geometry ----------------------------------------------------
# APPROACH the object, do not sit in it.  Parking the kart at the object's
# centre every frame found nothing at any height, including z = 0 on the
# floor - which is either a real finding or a broken rig, and a rig that
# never reproduces a known collision cannot tell you which.  So each trial
# starts the kart BACK units short, pointed at the object and moving, and
# runs a few frames: the same shape as tools/labs/objhit.py, which is the
# rig that did produce a crash (NOTES 150).
BACK   = int(os.environ.get("BACK", "26"))
SPEED  = int(os.environ.get("SPEED", "480"))
TRIAL  = int(os.environ.get("TRIAL", "8"))     # frames per approach
HEAD_S = 0x8000                                # 0 = north, $8000 = south

print("  trial   z at contact   hit?   speed in/out   $10    $AC", flush=True)
trials = []
for t in range(int(os.environ.get("SWEEP", "1400")) // TRIAL):
    ox, oy = rw(BLK + 0x18), rw(BLK + 0x1C)
    ww(0x1000 + 0x18, ox);      w[0x1000 + 0x16] = 0; w[0x1000 + 0x17] = 0
    ww(0x1000 + 0x1C, oy - BACK); w[0x1000 + 0x1A] = 0; w[0x1000 + 0x1B] = 0
    ww(0x1000 + 0xA4, HEAD_S); ww(0x1000 + 0xA2, HEAD_S); ww(0x1000 + 0x2A, HEAD_S)
    ww(0x1000 + 0xA8, 0)
    ww(0x1000 + 0xEA, SPEED)
    ww(0x1000 + 0x22, 0); ww(0x1000 + 0x24, SPEED)
    z0 = s16(rw(BLK + 0x1F))
    hit, zhit, out, f10, fac = 0, z0, SPEED, 0, 0
    for f in range(TRIAL):
        b.reg_reads[0x4219] = 0x80
        c.run_frames_scanline(1)
        f10 = rw(0x1000 + 0x10); fac = w[0x1000 + 0xAC]
        out = s16(rw(0x1000 + 0xEA))
        if (f10 & HIT_BIT) or fac == KNOCK or out < SPEED // 2:
            hit = 1; zhit = s16(rw(BLK + 0x1F)); break
    trials.append((z0, zhit, hit, out, f10, fac))
    if t % 10 == 0 or hit:
        print("  %5d %8d %8s %6d/%-5d $%04X   $%02X"
              % (t, zhit, "HIT" if hit else "-", SPEED, out, f10, fac), flush=True)

hits = [x for x in trials if x[2]]
miss = [x for x in trials if not x[2]]
print("\n  %d trials: %d hit, %d passed through" % (len(trials), len(hits), len(miss)),
      flush=True)
if hits and miss:
    hz = sorted(x[1] for x in hits); mz = sorted(x[1] for x in miss)
    print("    hit  at z %d..%d (median %d)" % (hz[0], hz[-1], hz[len(hz)//2]), flush=True)
    print("    pass at z %d..%d (median %d)" % (mz[0], mz[-1], mz[len(mz)//2]), flush=True)
    best = None
    for th in range(0, 13000, 32):
        err = sum(1 for z in hz if z >= th) + sum(1 for z in mz if z < th)
        if best is None or err < best[0]: best = (err, th)
    print("    the height that best separates them: %d  (misses %d of %d)"
          % (best[1], best[0], len(trials)), flush=True)
    print("    our port currently uses %d" % (4096 // 2), flush=True)
elif hits:
    print("    EVERY trial hit - height does not gate this object at all", flush=True)
else:
    print("    NO trial hit.  Either this object is never solid, or the rig"
          " cannot see a collision - run the pipe control before believing it",
          flush=True)
