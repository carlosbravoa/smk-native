#!/usr/bin/env python3
"""Every VRAM-bound DMA from reset to the settled attract race, with the
VRAM word address it landed at: source bank/address, bytes, destination.
ROM sources are tile data in place; WRAM sources were decompressed there."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from lab import log
from smktool.rom import Rom
from smktool.cpu import CPU, Bus, M_, X_
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cup, course = (int(sys.argv[1]), int(sys.argv[2])) if len(sys.argv) > 2 else (None, None)
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
b = Bus(bytes(r.data)); c = CPU(b)
b.log_dma = True
dests = []
orig_write = b.write
def wr(bank, addr, val):
    if (addr & 0xFFFF) == 0x420B and (bank & 0x7F) <= 0x3F:
        dests.append((val, b.regs.get(0x2116, 0) | b.regs.get(0x2117, 0) << 8, len(b.dma_log)))
    return orig_write(bank, addr, val)
b.write = wr
c.PB, c.PC = 0x80, r.vectors()["emu.RESET"]; c.P = M_ | X_; c.S = 0x1FFF
c.run_to(0x80805C, budget=8_000_000)
orig = b.read
def rd(bank, addr):
    lo = bank & 0x7F
    if lo <= 0x3F or bank == 0x7E:
        if addr in (0x0E32, 0x0E33, 0x0E50, 0x0E51): return 0
        if cup is not None and addr == 0x0150: return cup
        if cup is not None and addr == 0x0152: return course
    return orig(bank, addr)
b.read = rd
b.reg_reads[0x4218] = 0; b.reg_reads[0x4219] = 0
t0 = time.time()
while time.time() - t0 < 900:
    c.run_frames_scanline(10)
    if b.wram[0x36] // 2 in (1, 6) and sum(1 for k in range(128) if b.oam[k*4+1] not in (0, 0xF0, 0xE0)) >= 10: break
c.run_frames_scanline(30)
# pair each 420B trigger with the log entries it produced
seen = {}
for i, (en, vaddr, before) in enumerate(dests):
    after = dests[i+1][2] if i + 1 < len(dests) else len(b.dma_log)
    for (bank, src, bbad, n) in b.dma_log[before:after]:
        if bbad != 0x18: continue
        key = (vaddr, bank, src, n); seen[key] = seen.get(key, 0) + 1
for (vaddr, bank, src, n), cnt in sorted(seen.items()):
    print("VRAM $%04X (spr tile $%03X if OBJ)  <- %02X:%04X  %5d bytes (%3d tiles) x%d" % (vaddr, (vaddr - 0x4000) // 16 if vaddr >= 0x4000 else -1, bank, src, n, n // 32, cnt))
log("done: %d VRAM DMAs, %d triggers" % (sum(seen.values()), len(dests)))
