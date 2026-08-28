"""WHO writes the start grid?  Watch $1018 (P1 x) and $101C (P1 y) and
record the PC of every store, instead of assuming $819212 (NOTES 142b).
"""
import sys, os, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup = int(sys.argv[1]) if len(sys.argv) > 2 else 0
course = int(sys.argv[2]) if len(sys.argv) > 2 else 0

r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig_read = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33): return 0
        if addr == 0x0150: return cup
        if addr == 0x0152: return course
    return orig_read(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0

WATCH = {0x1018, 0x1019, 0x101C, 0x101D, 0x102A, 0x102B}
hits = []
orig_write = b.write
def wr(bank, addr, val):
    if addr in WATCH and ((bank & 0x7F) <= 0x3F or bank == 0x7E):
        hits.append((c.PB, c.PC, addr, val,
                     b.wram[0x0C] | b.wram[0x0D] << 8, b.wram[0x0E],
                     b.wram[0x12] | b.wram[0x13] << 8,
                     b.wram[0x14] | b.wram[0x15] << 8))
    orig_write(bank, addr, val)
b.write = wr

t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and (b.wram[0x1018] or b.wram[0x1019]):
        if sum(1 for k in range(128)
               if b.oam[k * 4 + 1] not in (0, 0xF0, 0xE0)) >= 10:
            break
print("track $%02X  writes seen: %d" % (b.wram[0x0124], len(hits)), flush=True)
seen = {}
for pb, pc, addr, val, p0c, p0e, d12, d14 in hits:
    seen.setdefault((pb, pc, addr), 0)
    seen[(pb, pc, addr)] += 1
for (pb, pc, addr), n in sorted(seen.items()):
    print("  $%02X:%04X -> $%04X   x%d" % (pb, pc, addr, n), flush=True)
print("--- last 24 in order ---", flush=True)
for pb, pc, addr, val, p0c, p0e, d12, d14 in hits[-24:]:
    print("  $%02X:%04X  $%04X = $%04X  [$0C]=$%02X:%04X $12=%d $14=%d"
          % (pb, pc, addr, val, p0e, p0c, d12, d14), flush=True)
