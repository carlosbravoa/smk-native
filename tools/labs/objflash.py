"""Rainbow Road's Thwomps flash.  Is it CGRAM, or the sprite's palette bits?

The user: "rainbow road thwomps have flashy colors to show they cannot be
touched".  A capture already showed +$06 in the object block cycling
through 65 values while x, y and z sit still (NOTES 152), so something is
animating - but that could be an art frame, a palette index, or the game
rewriting CGRAM every frame.  Watch CGRAM itself and find out.
"""
import sys, os, time
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools")
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

cup, course, want = 3, 4, 5          # Rainbow Road
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
        if sum(1 for k in range(128) if b.oam[k*4+1] not in (0,0xF0,0xE0)) >= 10:
            break
w = b.wram
def rw(a): return w[a] | w[a+1] << 8
for _ in range(900):
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
    if rw(0x1000+0xEA) > 100: break
t0 = time.time()
while time.time() - t0 < 1500:
    x, y = rw(0x1000+0x18), rw(0x1000+0x1C)
    cell = ((y >> 4) & 63) * 64 + ((x >> 4) & 63)
    d = ((w[0x14000 + cell] << 8) - rw(0x1000+0xA4)) & 0xFFFF
    if d > 32768: d -= 65536
    b.reg_reads[0x4219] = 0x80 | (0x02 if d < -0x300 else 0x01 if d > 0x300 else 0)
    c.run_frames_scanline(1)
    if (rw(0x1000 + 0xC0) >> 8) >= 0x81: break
print("track $%02X lap $%04X - Thwomps active" % (w[0x0124], rw(0x1000+0xC0)), flush=True)

seen = {i: set() for i in range(256)}
for f in range(240):
    b.reg_reads[0x4219] = 0x80
    c.run_frames_scanline(1)
    for i in range(256):
        seen[i].add(b.cgram[i*2] | b.cgram[i*2+1] << 8)
    if f < 6 or f % 40 == 0:
        print("  f%3d  blk0 +$06=%d  CGRAM $FA-$FF: %s" %
              (f, rw(0x1806),
               " ".join("%04X" % (b.cgram[i*2] | b.cgram[i*2+1] << 8)
                        for i in range(0xFA, 0x100))), flush=True)
ch = [(i, sorted(v)) for i, v in seen.items() if len(v) > 1]
print("\nCGRAM entries that changed over 240 frames: %d" % len(ch))
for i, v in ch[:24]:
    print("   $%02X  %d values  %s" % (i, len(v), ["%04X" % x for x in v[:8]]))
