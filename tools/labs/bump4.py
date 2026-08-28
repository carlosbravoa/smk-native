"""Is the box really +-4 px, and what do the WEIGHT cases do?

$81982A reads as |dx| < 4 and |dy| < 4.  A sweep is only meaningful if
the pairing state is cleared between trials - $81988C sets $50 (the
partner), $52, $10 bit 12 and the $5E cooldown on BOTH karts, and any of
them left set blocks the next contact.

Each trial: separate the pair along the road, clear all five fields, put
them back at the offset, step one frame, and see whether $819BBD fires.
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

P1 = 0x1000
def clear_pair(V):
    for base in (P1, V):
        sw(base + 0x5E, 0); sw(base + 0x50, 0); sw(base + 0x52, 0)
        sw(base + 0x10, w(base + 0x10) & ~0x1000)

def trial(V, dx, dy, wx=None, wy=None, vpx=0, vpy=-600, vvx=0, vvy=-400):
    # hold them apart for a few frames so nothing is latched
    keep = (w(P1 + 0x18), w(P1 + 0x1C))
    sw(P1 + 0x1C, (w(V + 0x1C) + 64) & 0xFFFF)
    sw(P1 + 0x18, w(V + 0x18))
    for _ in range(4):
        clear_pair(V)
        b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
    clear_pair(V)
    if wx is not None: sw(P1 + 0x4E, wx)
    if wy is not None: sw(V + 0x4E, wy)
    sw(P1 + 0x18, (w(V + 0x18) + dx) & 0xFFFF)
    sw(P1 + 0x1C, (w(V + 0x1C) + dy) & 0xFFFF)
    sw(P1 + 0x22, vpx); sw(P1 + 0x24, vpy & 0xFFFF)
    sw(V + 0x22, vvx); sw(V + 0x24, vvy & 0xFFFF)
    sw(P1 + 0xEA, int((vpx**2 + vpy**2) ** 0.5))
    sw(V + 0xEA, int((vvx**2 + vvy**2) ** 0.5))
    fired[0] = 0
    b.reg_reads[0x4219] = 0x80; c.run_frames_scanline(1)
    return (fired[0], s16(w(P1+0x22)), s16(w(P1+0x24)),
            s16(w(V+0x22)), s16(w(V+0x24)), w(P1+0xE2), w(V+0xE2))

V = 0x1200
print("== the box (equal weights) ==", flush=True)
print(" dx  dy | fired | P1 (%5s,%5s) | vk (%5s,%5s)" % ("vx","vy","vx","vy"), flush=True)
for dx, dy in [(0,0),(1,0),(2,0),(3,0),(4,0),(5,0),(6,0),
               (0,1),(0,3),(0,4),(0,5),(3,3),(-3,-3),(-4,0),(0,-4)]:
    f, px, py, vx, vy, pe, ve = trial(V, dx, dy)
    print("%3d %3d | %5d | (%5d,%5d) | (%5d,%5d)" % (dx, dy, f, px, py, vx, vy), flush=True)

print("\n== does the $819CC9 separation ever fire? ==", flush=True)
# after the exchange these still converge on BOTH axes, which is the
# condition $819B94 tests; if the shove runs, an $80 shows up.
for vpx, vpy, vvx, vvy in ((300, -400, 500, -200), (-300, -400, -500, -200),
                           (400, 300, 200, 500), (600, -100, 200, -300)):
    f, px, py, vx, vy, pe, ve = trial(V, 0, 0, 0x1A, 0x1A, vpx, vpy, vvx, vvy)
    print("  in P1 (%5d,%5d) vk (%5d,%5d) | fired %d | out P1 (%5d,%5d) vk (%5d,%5d)"
          % (vpx, vpy, vvx, vvy, f, px, py, vx, vy), flush=True)

print("\n== the weight cases at dx=0 (P1 $4E forced) ==", flush=True)
for wx, wy, tag in ((0x1A, 0x1A, "equal"), (0x1B, 0x1A, "X +1"),
                    (0x1B, 0x19, "X +2"), (0x19, 0x1B, "X -2")):
    f, px, py, vx, vy, pe, ve = trial(V, 0, 0, wx, wy)
    print("  %-6s $4E %02X/%02X | fired %d | P1 (%5d,%5d) E2 %04X | vk (%5d,%5d) E2 %04X"
          % (tag, wx, wy, f, px, py, pe, vx, vy, ve), flush=True)
