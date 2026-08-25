"""Can we generate the entity tiles from the ROM, byte for byte?

The game decompresses $C7:0000 to $7F:4400 and expands it into the
sprite staging with a copy-16 / zero-16 loop ($81:E5A0), so tile n
should be source[n*16..n*16+15] as 2bpp with the high planes zero.
Comparing SHAPES by eye was inconclusive last time; compare bytes.
"""
import os, sys
from lab import Lab, log

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))
from smktool.compress import decompress

L = Lab()
ob = (L.b.regs.get(0x2101, 0) & 7) * 0x4000
log("OBSEL base $%04X" % ob)

blob, used = decompress(bytes(L.r.data), L.r.snes_to_pc(0xC70000), max_out=0x10000)
log("blob %d bytes" % len(blob))

# what the game actually staged, and what reached VRAM
stage = bytes(L.b.wram[0x14400:0x14400 + 0x1000])   # $7F:4400
log("staging $7F:4400 matches the blob: %s"
    % ("YES" if stage[:len(blob)] == bytes(blob) else "NO"))
if stage[:64] != bytes(blob[:64]):
    log("  blob[0:16]  %s" % " ".join("%02X" % b for b in blob[:16]))
    log("  stage[0:16] %s" % " ".join("%02X" % b for b in stage[:16]))

for t in (0xCE, 0xCF, 0xD0, 0xD7):
    vram = bytes(L.b.vram[(ob + t * 32):(ob + t * 32) + 32])
    src = bytes(blob[t * 16:t * 16 + 16])
    built = src + bytes(16)                 # 2bpp widened, high planes zero
    log("tile $%02X: vram==built %s" % (t, "YES" if vram == built else "NO"))
    if vram != built:
        log("   vram  %s" % " ".join("%02X" % b for b in vram[:16]))
        log("   built %s" % " ".join("%02X" % b for b in built[:16]))
        # where in the blob does this tile's data actually live?
        hit = bytes(blob).find(vram[:16])
        log("   vram's first 16 bytes found in blob at %s"
            % (("offset %d (= tile %.2f)" % (hit, hit / 16.0)) if hit >= 0
               else "NOT FOUND"))
