"""The Thwomp's lift, from the screen position the game itself computes.

$80C851/$80C86B write every object's projected screen position into the
block at +$2C (x) and +$2E (y) BEFORE the on-screen tests at $80C885 and
$80C88F decide whether to draw it - so +$2C/+$2E are readable even for an
object the game then declines to draw, which at z = 4096 is most of them:
a parked Thwomp is lifted clean off the top of the screen.

The pair that matters is the Thwomp's own +$2E against its shadow
sub-block's (+$40 +$2E) at the same +$1F.  Their difference is the lift in
screen pixels, which is the $1F -> screen conversion the port is missing.
"""
import sys, os, time, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
KH = 0xBE5E
cup, course, want = 0, 3, 17
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
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
while time.time() - t0 < 1800:
    c.run_frames_scanline(10)
    if b.wram[0x0124] == want and b.wram[0x36] // 2 in (1, 6):
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
w = b.wram
def rw(a): return w[a] | w[a+1] << 8
def sw(a, v): w[a] = v & 0xFF; w[a+1] = (v >> 8) & 0xFF
def s16(v): return v - 65536 if v > 32767 else v
print("reached race: track %d" % w[0x0124], flush=True)
for _ in range(700):
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)

BLK = [0x1800, 0x1880]
print("blocks: " + ", ".join("$%04X (%d,%d)" % (a, rw(a+0x18), rw(a+0x1C))
                             for a in BLK), flush=True)
print("\nscreenX = $2C + $110, screenY = $2E + $120  ($80C885/$80C88F)")
print("\n   D  blk     zf   $06 | thwomp sx   sy | shadow sx   sy | LIFT |    z",
      flush=True)
for D in list(range(16, 340, 4)):
    for _ in range(3):
        sw(0x1000+0x18, 388 + D); sw(0x1000+0x1C, 38)
        sw(0x1000+0xA4, KH); sw(0x1000+0x2A, KH)
        sw(0x1000+0xEA, 0); sw(0x1000+0x22, 0); sw(0x1000+0x24, 0)
        b.reg_reads[0x4219] = 0; c.run_frames_scanline(1)
    for a in BLK:
        ox, oy = rw(a+0x18), rw(a+0x1C)
        if not (0 < ox < 1024): continue
        mx = s16(rw(a+0x2C)) + 0x110; my = s16(rw(a+0x2E)) + 0x120
        sx = s16(rw(a+0x40+0x2C)) + 0x110; sy = s16(rw(a+0x40+0x2E)) + 0x120
        z = s16(rw(a+0x1F))
        ang = (KH + 0x00C0) / 65536.0 * 2 * math.pi - math.pi / 2
        zf = (ox - (388 + D)) * math.cos(ang) + (oy - 38) * math.sin(ang)
        print("%4d $%04X %6.1f %5d | %8d %4d | %8d %4d | %4d | %5d"
              % (D, a, zf, rw(a+0x06), mx, my, sx, sy, sy - my, z), flush=True)
