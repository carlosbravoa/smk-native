"""The MC2 crossing jump in the oracle: force P1 onto the ramp at x 552,
y 698 heading east at several speeds, and log z, zvel, speed, x per frame."""
import sys, os
sys.path.insert(0, "/home/carlos/extended/devel/games/mariokart/tools/labs")
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
import time
P1 = 0x1000
# NOTES 118's way to another course: hook the cup/course reads, so mode
# entry computes $0124 and the theme itself (forcing $0124 is the trap)
r = Rom.load("/home/carlos/extended/devel/games/mariokart/rom/smk_usa.sfc")
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig_read = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return 0
        if addr == 0x0152: return 4
    return orig_read(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 1200:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and (b.wram[0x1018] or b.wram[0x1019]):
        if sum(1 for k in range(128) if b.oam[k * 4 + 1] not in (0, 0xF0, 0xE0)) >= 10:
            break
c.run_frames_scanline(400)      # past the countdown
log("track $0124 = %d" % (b.wram[0x0124] | b.wram[0x0125] << 8))
def w(a): return b.wram[a] | b.wram[a+1] << 8
def sw(a, v): b.wram[a] = v & 0xFF; b.wram[a+1] = (v >> 8) & 0xFF
def s16(v): return v - 65536 if v > 32767 else v
for K, who in ((0x1700, "AI kart 7"), (0x1000, "P1")):
    if K == 0x1000: b.reg_reads[0x4219] = 0x80      # throttle for the player
    lapb = b.wram[K+0xC1]
    # grounded ON the lower road, in sector 20's paint, having come from 29
    sw(K+0x18, 604); sw(K+0x1A, 0); sw(K+0x1C, 700); sw(K+0x1E, 0); b.wram[K+0x20] = 0
    sw(K+0x2A, 0x4000); sw(K+0xA4, 0x4000); sw(K+0xA2, 0x4000)
    sw(K+0x26, 0); sw(K+0xEA, 500); sw(K+0xE2, w(K+0xE2) & 0x7FFF)
    sw(K+0x22, 0); sw(K+0x24, 0)
    sw(K+0xC0, lapb << 8 | 29); sw(K+0xDC, lapb << 8 | 29); sw(K+0xF8, lapb << 8 | 29)
    log("%s placed at (604,700) heading east, sector 29: f x y spd C0 DC heading" % who)
    for f in range(100):
        c.run_frames_scanline(1)
        if f % 4 == 0: log("  f%3d %4d %4d %5d $%04X $%04X %5.1f" % (f, w(K+0x18), w(K+0x1C), s16(w(K+0xEA)), w(K+0xC0), w(K+0xDC), w(K+0x2A) * 360 / 65536))
