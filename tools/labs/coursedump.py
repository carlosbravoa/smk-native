#!/usr/bin/env python3
"""Boot straight into cup/course (the $0150/$0152 read hook gridtable.py
uses), settle, and save VRAM / CGRAM / OAM to tmp/*_c<cup><course>.bin."""
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
c.run_frames_scanline(int(os.environ.get("SETTLE", "120")))
tag = "_c%d%d" % (cup, course)
open("tmp/vram%s.bin" % tag, "wb").write(bytes(b.vram)); open("tmp/cgram%s.bin" % tag, "wb").write(bytes(b.cgram)); open("tmp/oam%s.bin" % tag, "wb").write(bytes(b.oam))
log("track $%02X theme $%02X saved%s" % (b.wram[0x0124], b.wram[0x0126], tag))
