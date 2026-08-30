#!/usr/bin/env python3
"""The Thwomps' cycle with the block words beside it: boot Bowser Castle 1,
force the lap to 2 so the movers wake, then log every frame each object
block's z (+$1E) and its whole first 32 words - so the counter that ends
the RISE (NOTES 152's one unpinned number) can be read off the block
instead of guessed."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup, course = 0, 3
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33, 0x0E50, 0x0E51): return 0
        if addr == 0x0150: return cup
        if addr == 0x0152: return course
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and sum(1 for k in range(128) if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
c.run_frames_scanline(420)              # through the lights
def w(a): return b.wram[a] | b.wram[a+1] << 8
def s16(v): return v - 65536 if v > 32767 else v
log("track $%02X theme $%02X; lap byte $%02X" % (b.wram[0x0124], b.wram[0x0126], b.wram[0x10C1]))
b.wram[0x10C1] = 0x81; b.wram[0x10F9] = 0x81
print("f," + ",".join("b%d_z,b%d_words" % (i, i) for i in range(4)), flush=True)
for f in range(2400):
    b.reg_reads[0x4219] = 0x80      # B held
    c.run_frames_scanline(1)
    cols = [str(f)]
    for i in range(4):
        base = 0x1800 + i * 0x80
        cols.append(str(s16(w(base + 0x1E))))
        cols.append(' '.join("%04X" % w(base + j) for j in range(0, 0x40, 2)))
    print(",".join(cols), flush=True)
log("done")
