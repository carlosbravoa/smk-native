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
#
# CHECK THE ARITHMETIC BEFORE THE RUN.  A kart covers roughly speed/280
# world units a frame (measured: 3 units a frame at speed ~900), so the
# trial must be long enough to CROSS the gap - the first attempt used 8
# frames at speed 480, which travels 16 units at a gap of 26, and duly
# reported "no hit at any height" because the kart never arrived.
# 24 frames at 750 covers about 64 units, which crosses comfortably.
BACK   = int(os.environ.get("BACK", "26"))
SPEED  = int(os.environ.get("SPEED", "750"))
TRIAL  = int(os.environ.get("TRIAL", "24"))    # frames per approach
IDLE   = int(os.environ.get("IDLE", "20"))     # frames between them,
                                               # so the cycle advances
HEAD_S = 0x8000                                # 0 = north, $8000 = south

import math

def flow_head(x, y):
    """the heading the game's own flow field wants at (x,y) - $7F:4000"""
    cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
    return w[0x14000 + cell] << 8

def approach(ox, oy, back):
    """where to start, to meet (ox,oy) the way a driven kart would.

    Coming from a fixed compass direction is how the last run went wrong:
    the object sits at (388,68), so a kart placed 26 units NORTH of it
    starts at y=42, off the road near the map edge, and hits the terrain.
    That reads exactly like hitting the object - same $10, same $AC, same
    halved speed - and it hit at EVERY height, which is what a wall does.
    Approaching along the flow field puts the kart on the road, coming at
    the object the way a real one does."""
    h = flow_head(ox, oy)
    a = h * math.pi / 32768.0
    dx, dy = math.sin(a), -math.cos(a)
    return int(ox - back * dx), int(oy - back * dy), h

def run_approach(ox, oy, h, sx, sy):
    ww(0x1000 + 0x18, sx); w[0x1000 + 0x16] = 0; w[0x1000 + 0x17] = 0
    ww(0x1000 + 0x1C, sy); w[0x1000 + 0x1A] = 0; w[0x1000 + 0x1B] = 0
    ww(0x1000 + 0xA4, h); ww(0x1000 + 0xA2, h); ww(0x1000 + 0x2A, h)
    ww(0x1000 + 0xA8, 0)
    ww(0x1000 + 0xEA, SPEED)
    a = h * math.pi / 32768.0
    ww(0x1000 + 0x22, int(SPEED * math.sin(a)) & 0xFFFF)
    ww(0x1000 + 0x24, int(-SPEED * math.cos(a)) & 0xFFFF)
    hit, zh = 0, s16(rw(BLK + 0x1F))
    for _ in range(TRIAL):
        b.reg_reads[0x4219] = 0x80
        c.run_frames_scanline(1)
        if not hit and ((rw(0x1000 + 0x10) & HIT_BIT)
                        or w[0x1000 + 0xAC] == KNOCK
                        or s16(rw(0x1000 + 0xEA)) < SPEED // 2):
            hit = 1; zh = s16(rw(BLK + 0x1F))
    return hit, zh

print("  Every trial is run TWICE from the same start: once with the object"
      " where it is,", flush=True)
print("  and once with it moved off the map.  Only a hit that disappears when"
      " the object", flush=True)
print("  does is a hit on the OBJECT; anything else is the track.", flush=True)
print("\n  trial      z   object?  no-object?   verdict", flush=True)
trials = []
for t in range(int(os.environ.get("SWEEP", "5000")) // (2 * TRIAL + IDLE)):
    ox, oy = rw(BLK + 0x18), rw(BLK + 0x1C)
    sx, sy, h = approach(ox, oy, BACK)
    z0 = s16(rw(BLK + 0x1F))
    hit_a, zh = run_approach(ox, oy, h, sx, sy)
    # the same approach with the object taken away
    ww(BLK + 0x18, 4000); ww(BLK + 0x1C, 4000)
    hit_b, _ = run_approach(ox, oy, h, sx, sy)
    ww(BLK + 0x18, ox); ww(BLK + 0x1C, oy)
    v = ("THE OBJECT" if hit_a and not hit_b else
         "the track" if hit_a and hit_b else
         "nothing" if not hit_a else "?")
    trials.append((zh, hit_a, hit_b, v))
    if t % 3 == 0 or v == "THE OBJECT":
        print("  %5d %6d %8s %10s   %s"
              % (t, zh, "HIT" if hit_a else "-", "HIT" if hit_b else "-", v),
              flush=True)
    for _ in range(IDLE):
        b.reg_reads[0x4219] = 0x80
        c.run_frames_scanline(1)

obj = [x for x in trials if x[1] and not x[2]]
trk = [x for x in trials if x[1] and x[2]]
non = [x for x in trials if not x[1]]
print("\n  %d trials: %d hit the OBJECT, %d hit the track, %d hit nothing"
      % (len(trials), len(obj), len(trk), len(non)), flush=True)
if obj:
    zs = sorted(x[0] for x in obj)
    print("    the object was hit at z %d..%d" % (zs[0], zs[-1]), flush=True)
    passed = sorted(x[0] for x in non)
    if passed:
        print("    it was passed at z %d..%d" % (passed[0], passed[-1]), flush=True)
        best = None
        for th in range(0, 13000, 32):
            err = sum(1 for z in zs if z >= th) + sum(1 for z in passed if z < th)
            if best is None or err < best[0]: best = (err, th)
        print("    best separating height: %d (misses %d)  - our port uses %d"
              % (best[1], best[0], 4096 // 2), flush=True)
    else:
        print("    and never passed - height does not gate it", flush=True)
