#!/usr/bin/env python3
"""The HUD palette's source, through the DMA the game actually uses.

The first hudpal hooked Bus.write for $2122 and could not see palette
uploads, because a DMA to the B bus goes through _ppu_write.  This hooks
that, logs every DMA to $22 (CGRAM data) with its WRAM source, and then
hooks WRITES to that source range for palettes 4-7 with the PC - which is
the ROM routine that decides the colours, and where any animation lives.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab, log

TMP = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tmp")
L = Lab(settle=30, zero=(0x0E50, 0x0E51))
b, c = L.b, L.c
frame = [0]
b.log_dma = True
cg = []                         # (frame, cgadd, val) for CGRAM bytes 32..63
orig_ppu = b._ppu_write
def ppu_hook(reg, val):
    if reg == 0x2122 and 32 <= (b.cgadd & 0x1FF) < 64:
        cg.append((frame[0], b.cgadd & 0x1FF, val))
    return orig_ppu(reg, val)
b._ppu_write = ppu_hook
for _ in range(120):
    L.flow(1); frame[0] += 1
dmas = [d for d in b.dma_log if d[2] == 0x22]
log("CGRAM DMAs seen in 120 frames: %d; distinct sources: %s" % (len(dmas), sorted(set((d[0], d[1], d[3]) for d in dmas))[:6]))
log("$2122 writes into palettes 4-7: %d" % len(cg))
if not dmas:
    log("no CGRAM DMA: the palette is not re-uploaded per frame - hooking is moot"); sys.exit(0)
srcbank, src, _, count = dmas[-1]
# the WRAM buffer bytes 32..63 are palettes 4-7; hook writes to them with the PC
lo, hi = src + 32, src + 64
wr = []
orig_write = b.write
def whook(bank, addr, val):
    if (bank & 0x7F) <= 0x3F or bank == 0x7E:
        if lo <= addr < hi:
            wr.append((frame[0], addr - src, val, (c.PB << 16) | c.PC))
    return orig_write(bank, addr, val)
b.write = whook
log("palette buffer at $%02X:%04X (%d bytes); now: pal4 %s  pal6 %s" % (srcbank, src, count,
    " ".join("%04X" % (b.wram[src + i*2] | b.wram[src + i*2 + 1] << 8) for i in range(16, 20)) if srcbank in (0x7E, 0) else "?",
    " ".join("%04X" % (b.wram[src + i*2] | b.wram[src + i*2 + 1] << 8) for i in range(24, 28)) if srcbank in (0x7E, 0) else "?"))
L.sw(0x0D70, 0xA000); L.sw(0x0D78, 0xC1); L.sw(0x0D74, 0xB49D); L.sw(0x0D7C, 4)
for f in range(320):
    L.flow(1); frame[0] += 1
    if f in (10, 200, 300):
        log("f%d: $0D70=$%04X  CGRAM pal4 %s  pal6 %s  pal7 %s" % (f, L.w(0x0D70),
            " ".join("%04X" % (b.cgram[i*2] | b.cgram[i*2+1] << 8) for i in range(16, 20)),
            " ".join("%04X" % (b.cgram[i*2] | b.cgram[i*2+1] << 8) for i in range(24, 28)),
            " ".join("%04X" % (b.cgram[i*2] | b.cgram[i*2+1] << 8) for i in range(28, 32))))
log("writes into the buffer's palettes 4-7: %d" % len(wr))
seen = {}
for f, off, v, pc in wr: seen.setdefault(pc, []).append((f, off, v))
for pc, ws in sorted(seen.items()):
    log("   $%06X: %d writes; first f%d offset %d = %02X; last f%d" % (pc, len(ws), ws[0][0], ws[0][1], ws[0][2], ws[-1][0]))
