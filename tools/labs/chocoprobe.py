#!/usr/bin/env python3
"""Boot Choco Island (cup/course args), let the race idle, and log the
live entity blocks every 20 frames - do the game's moles move on their
own, and in which fields?  (bug 12)"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup, course = int(sys.argv[1]), int(sys.argv[2])
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
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and sum(1 for k in range(128) if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
else: raise SystemExit("never reached the race")
c.run_frames_scanline(120)
def u16(a): return b.wram[a] | b.wram[a+1] << 8
log("track $%02X theme $%02X" % (b.wram[0x0124], b.wram[0x0126]))
for f in range(0, 900, 20):
    row=[]
    for base in range(0x1800, 0x1A00, 0x40):
        if u16(base):
            row.append("%04X@(%d,%d)z%04X f1E%04X f20%04X f26%04X" % (
                u16(base), u16(base+0x18), u16(base+0x1C),
                u16(base+0x1E), u16(base+0x20), u16(base+0x26), u16(base+0x2A)))
    log("f%03d %s" % (f, " | ".join(row)))
    c.run_frames_scanline(20)
