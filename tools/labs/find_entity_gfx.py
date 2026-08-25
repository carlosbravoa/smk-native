"""Where do the entity tiles actually come from?

NOTES 086 guessed $C7:0000 from a nearby decompress call; a byte
comparison (entity_art.py) shows that is wrong - the staging does not
hold that blob and the live tiles are not in it.

So search: decompress every plausible stream in the graphics banks and
look for the signature bytes of a live entity tile.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from smktool.rom import Rom
from smktool.compress import decompress, CompressionError

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
r = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
data = bytes(r.data)

# 16 bytes of live VRAM tile $CE and $CF (from the running game)
SIGS = {
    "$CE": bytes.fromhex("16271627162716271627162716271921"),
    "$CF": bytes.fromhex("68e468e468e468e468e468e468e49884"),
    "$D0": bytes.fromhex("04080408040804080304000703040204"),
}

print("scanning graphics banks for streams containing the live tiles...")
hits = 0
for bank in range(0xC0, 0xD0):
    base = bank << 16
    try:
        start = r.snes_to_pc(base)
    except Exception:
        continue
    if start + 0x10000 > len(data):
        continue
    # try a stream every 2 bytes; cheap enough and catches unaligned tables
    off = 0
    while off < 0x10000:
        try:
            out, used = decompress(data, start + off, max_out=0x8000)
        except (CompressionError, Exception):
            off += 2
            continue
        if len(out) >= 256:
            ob = bytes(out)
            for name, sig in SIGS.items():
                k = ob.find(sig)
                if k >= 0:
                    print("  HIT %s: stream $%02X:%04X (%d bytes out) "
                          "contains tile %s at offset %d (tile %.2f)"
                          % (name, bank, off, len(out), name, k, k / 16.0))
                    hits += 1
        off += max(used, 2) if len(out) >= 256 else 2
print("done, %d hits" % hits)
