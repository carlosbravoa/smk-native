"""The kart-to-kart box, measured: at what separation does the response
fire, and what does it do?

$81982A tests |dx| < 4 and |dy| < 4 on the two karts' pixel positions and
then runs $819B06, which swaps their velocity vectors ($819CB8) and, if
they are still converging, shoves the trailing one by $80 ($819CC9).
This sweeps the separation in the running game and watches for the write
at $819BBD, so the box is a measurement rather than a reading.

    python3 tools/labs/bump2.py
"""
import sys, os, time
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
def sw(a, v): b.wram[a] = v & 0xFF; b.wram[a + 1] = (v >> 8) & 0xFF
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
for _ in range(120):
    b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
    c.run_frames_scanline(1)

fired = [0]
orig_w = b.write
def wr(bank, addr, val):
    if addr in (0x1022, 0x1023) and c.PB == 0x81 and 0x9BB0 <= c.PC <= 0x9BD0:
        fired[0] += 1
    orig_w(bank, addr, val)
b.write = wr

P1, V = 0x1000, 0x1200          # P1 and kart block 2
print("dx dy | fired | P1 v(%5s,%5s) -> (%5s,%5s) | vk v -> v   | $4E P1/vk"
      % ("vx", "vy", "vx", "vy"), flush=True)
TRIALS = [(dx, 0) for dx in (0, 1, 2, 3, 4, 5, 6, 8, 12, 16)] + \
         [(0, dy) for dy in (1, 2, 3, 4, 5, 6, 8, 12)] + \
         [(-3, 0), (0, -3), (3, 3), (4, 4), (-4, -4)]
for dx, dy in TRIALS:
        # Park P1 far away and let the pairing state ($50 partner, $52,
        # $5E cooldown, $10 bit 12) clear before each trial - otherwise
        # only the first separation ever fires.
        sw(P1 + 0x18, (w(V + 0x18) + 400) & 0xFFFF)
        for _ in range(10):
            b.reg_reads[0x4219] = 0; c.run_frames_scanline(1)
        sw(0x105E, 0); sw(V + 0x5E, 0)
        sw(0x1050, 0); sw(V + 0x50, 0)
        sw(0x1052, 0); sw(V + 0x52, 0)
        sw(P1 + 0x18, (w(V + 0x18) + dx) & 0xFFFF)
        sw(P1 + 0x1C, (w(V + 0x1C) + dy) & 0xFFFF)
        sw(P1 + 0x22, 0); sw(P1 + 0x24, -600 & 0xFFFF)
        sw(V + 0x22, 0);  sw(V + 0x24, -400 & 0xFFFF)
        before = (s16(w(P1+0x22)), s16(w(P1+0x24)), s16(w(V+0x22)), s16(w(V+0x24)))
        fired[0] = 0
        b.reg_reads[0x4219] = 0x80; b.reg_reads[0x4218] = 0
        c.run_frames_scanline(1)
        after = (s16(w(P1+0x22)), s16(w(P1+0x24)), s16(w(V+0x22)), s16(w(V+0x24)))
        print("%2d %2d | %5d | (%5d,%5d) -> (%5d,%5d) | (%5d,%5d) -> (%5d,%5d) | %04X %04X"
              % (dx, dy, fired[0], before[0], before[1], after[0], after[1],
                 before[2], before[3], after[2], after[3],
                 w(P1 + 0x4E), w(V + 0x4E)), flush=True)

