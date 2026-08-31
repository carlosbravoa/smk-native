#!/usr/bin/env python3
"""Choco Island: park the player ON a live entity and log every word of
its block that changes - does the mole pop for a nearby kart? (bug 12)"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return 1
        if addr == 0x0152: return 0
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and sum(1 for k in range(128) if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
else: raise SystemExit("never reached the race")
c.run_frames_scanline(400)          # let the countdown finish
def u16(a): return b.wram[a] | b.wram[a+1] << 8
def put16(a, v): b.wram[a] = v & 0xFF; b.wram[a+1] = (v >> 8) & 0xFF
log("track $%02X, parking P1 on the (188,388) entity" % b.wram[0x0124])
prev = {}
def scan(tag):
    for base in (0x1880, 0x18C0, 0x1800, 0x1840):
        words = tuple(u16(base+o) for o in range(0, 0x40, 2))
        if prev.get(base) != words:
            diff = " ".join("+%02X=%04X" % (o*2, w) for o, w in enumerate(words)
                            if prev.get(base) and prev[base][o] != w)
            if not prev.get(base):
                diff = " ".join("%04X" % w for w in words)
            log("%s blk %04X: %s" % (tag, base, diff))
            prev[base] = words
for f in range(0, 120, 10):
    scan("idle f%03d" % f); c.run_frames_scanline(10)
put16(0x1018, 188); put16(0x101C, 380)      # the kart onto the mole
for f in range(0, 600, 10):
    scan("near f%03d" % f)
    put16(0x1018, 188); put16(0x101C, 380)  # hold it there
    c.run_frames_scanline(10)
