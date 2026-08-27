"""Find the Thwomp and its shadow in OAM, and measure the real height.

A Thwomp is the only thing on screen whose Y moves a long way while its X
stays put (NOTES 152: motion is Z only).  Its shadow should be a sprite at
the same X that does NOT move.  That pair gives, straight from the game:

  * how far above the ground the Thwomp actually rises, in screen pixels
  * the shadow's own tile and palette, so it can be drawn from the ROM
    instead of invented

Thwomps are parked until the first lap is done, so drive one first.
"""
import sys, os, time
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools")
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

cup, course, want = (int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])) \
    if len(sys.argv) > 3 else (0, 3, 17)          # Bowser Castle 1
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
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10: break
w = b.wram
def rw(a): return w[a] | w[a+1] << 8
for _ in range(900):
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
    if rw(0x1000+0xEA) > 100: break
def drive(n):
    for _ in range(n):
        x, y = rw(0x1000+0x18), rw(0x1000+0x1C)
        cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
        d = ((w[0x14000 + cell] << 8) - rw(0x1000+0xA4)) & 0xFFFF
        if d > 32768: d -= 65536
        b.reg_reads[0x4219] = 0x80 | (0x02 if d < -0x300 else 0x01 if d > 0x300 else 0)
        c.run_frames_scanline(1)
t0 = time.time()
while time.time() - t0 < 1500:
    drive(1)
    if (rw(0x1000 + 0xC0) >> 8) >= 0x81: break
print("track $%02X lap $%04X - Thwomps active" % (w[0x0124], rw(0x1000+0xC0)), flush=True)

# OAM slots are rebuilt every frame, so a slot index is not an identity
# (the first version of this lab tracked slots and matched nothing).
# Anchor on the object instead: drive until the kart is close to a live
# block, then dump the whole sprite list beside that block's own z.
BLK = [0x1800, 0x1880]
import math
best = None
for f in range(4000):
    drive(1)
    kx, ky = rw(0x1000+0x18), rw(0x1000+0x1C)
    for a in BLK:
        ox, oy = rw(a+0x18), rw(a+0x1C)
        if not (0 < ox < 1024 and 0 < oy < 1024): continue
        d = math.hypot(kx-ox, ky-oy)
        if d < 110 and (best is None or d < best[0]):
            best = (d, a, ox, oy, f)
    if best and best[0] < 60: break
if not best:
    print("never got near a live object"); sys.exit(0)
d, a, ox, oy, f = best
print("closest approach %.0f px to block $%04X at (%d,%d)" % (d, a, ox, oy), flush=True)
for shot in range(6):
    kx, ky = rw(0x1000+0x18), rw(0x1000+0x1C)
    z = rw(a+0x1F); z = z-65536 if z > 32767 else z
    print("\n-- kart (%d,%d) head $%04X | block (%d,%d) z %d | dist %.0f"
          % (kx, ky, rw(0x1000+0xA4), rw(a+0x18), rw(a+0x1C), z,
             math.hypot(kx-rw(a+0x18), ky-rw(a+0x1C))), flush=True)
    ent = []
    for k in range(128):
        x, y, t_, at = b.oam[k*4:k*4+4]
        hi = b.oam[512 + (k >> 2)]
        xh = (hi >> ((k & 3) * 2)) & 1
        big = (hi >> ((k & 3) * 2 + 1)) & 1
        if y in (0, 0xF0, 0xE0): continue
        ent.append((y, x - (256 if xh else 0), t_ | ((at & 1) << 8), (at >> 1) & 7, big))
    ent.sort()
    for y, x, t_, pal, big in ent:
        print("     y %3d x %4d tile $%03X pal %d %s" % (y, x, t_, pal, "16x16" if big else "8x8"))
    drive(20)
