"""What a Thwomp actually DOES, frame by frame.

The spawner ($84DC20) fills object blocks whose addresses come from
$81:9194 = $1800/$1880/$1900/$1980, two of them live in a one-player race
($819136).  Inside a block: x at +$18, y at +$1C, height at +$1F, and the
script pointer at +$04 (a second script for the sub-block at +$40).

Rather than port the bytecode VM at $85E0B9 blind, log the height these
scripts produce and port THAT.  Usage: movers.py <cup> <course> <track>
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
print("track $%02X  $0D2C=%d  $0D28=%d" % (w[0x0124], rw(0x0D2C), rw(0x0D28)), flush=True)

print("live block table $1DA0:", " ".join("$%04X" % rw(0x1DA0 + i*2) for i in range(4)))

# Which bytes in the object blocks move at all?  Do not assume an offset -
# snapshot the whole region and diff it, driving the kart round so the
# player gets near whatever is out there.
LO, HI = 0x1800, 0x1A00
seen = {a: set() for a in range(LO, HI)}
for f in range(FRAMES):
    if True:
        print("   f%3d kart (%4d,%4d) spd %4d | blk0 (%4d,%4d) z %6d s $%04X"
              " | blk1 (%4d,%4d) z %6d s $%04X" %
              (f, rw(0x1000+0x18), rw(0x1000+0x1C), s16(rw(0x1000+0xEA)),
               rw(0x1818), rw(0x181C), s16(rw(0x181F)), rw(0x1804),
               rw(0x1898), rw(0x189C), s16(rw(0x189F)), rw(0x1884)), flush=True)
    # hold the throttle and bang-bang toward the flow field so we actually
    # travel and meet the objects
    x, y = rw(0x1000+0x18), rw(0x1000+0x1C)
    cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
    want_h = w[0x14000 + cell] << 8
    d = (want_h - rw(0x1000+0xA4)) & 0xFFFF
    if d > 32768: d -= 65536
    pad = 0x80 | (0x02 if d < -0x300 else 0x01 if d > 0x300 else 0)
    b.reg_reads[0x4219] = pad
    c.run_frames_scanline(1)
print("kart ended at (%d,%d) speed %d - if it did not move, the capture is void"
      % (rw(0x1000+0x18), rw(0x1000+0x1C), s16(rw(0x1000+0xEA))))
print("live blocks now:", " ".join("$%04X" % rw(0x1DA0 + i*2) for i in range(4)))
for a in (0x1800, 0x1880):
    print("  blk $%04X  x=%d y=%d  +$1F word=%d  script=$%04X"
          % (a, rw(a+0x18), rw(a+0x1C), s16(rw(a+0x1F)), rw(a+0x04)))
changed = []
print("\nbytes that changed over %d frames (%d of %d):" % (FRAMES, len(changed), HI-LO))
for a, v in changed:
    blk = (a - LO) // 0x40 * 0x40 + LO
    print("  $%04X  (blk $%04X +$%02X)  %d values  %s"
          % (a, blk, a - blk, len(v), v[:10]))
