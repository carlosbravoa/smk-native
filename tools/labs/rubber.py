"""The rubber band: what target-speed ROW does each AI kart get, and why?

$80AD5E computes a row into the target-speed table each frame and stores
it at $C8,x; src/ai.c reads that row but the port always passes 0.
$80AD96 picks it from the kart's RANK ($E6), the karts immediately ahead
and behind in rank order ($010C/$0110 + rank*2), a per-kart timer ($DA,
0..$3C) and the DSP-1 distance between them against a per-(class,rank)
threshold table at $80AF0F.

Rather than port that blind, log what it actually produces: every kart's
row, rank, speed, timer and distance to the kart ahead, once a second
through a race.

    python3 tools/labs/rubber.py
"""
import sys, os, time, math
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
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
        if addr in (0x0150, 0x0152): return 0
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
def w(a): return b.wram[a] | b.wram[a + 1] << 8
def s16(v): return v - 65536 if v > 32767 else v

t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and (b.wram[0x1018] or b.wram[0x1019]):
        if sum(1 for k in range(128)
               if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
for _ in range(600):
    b.reg_reads[0x4219] = 0; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if s16(w(0x0146)) == 0 and b.wram[0x3A] == 6: break
print("released.  P1 holds the throttle throughout.", flush=True)
print("  f | kart rank row  spd   $DA  dist-to-rank-ahead", flush=True)

for f in range(1500):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)
    if f % 120: continue
    rows = []
    for k in range(8):
        base = 0x1000 + k * 0x100
        rank = w(base + 0xE6) // 2
        ahead = w(0x010C + w(base + 0xE6)) if rank else 0
        d = ""
        if ahead:
            dx = s16(w(ahead + 0x18)) - s16(w(base + 0x18))
            dy = s16(w(ahead + 0x1C)) - s16(w(base + 0x1C))
            d = "%5d" % int(math.hypot(dx, dy))
        rows.append("k%d r%d row$%02X sp%4d DA%2d %s"
                    % (k, rank, w(base + 0xC8) & 0xFF, s16(w(base + 0xEA)),
                       w(base + 0xDA), d))
    print("%5d | %s" % (f, "  ".join(rows)), flush=True)
