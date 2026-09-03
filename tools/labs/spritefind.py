"""Shared bit for the two sprite-source labs: read the oracle's OAM the way
MAME's decoded object table reads, and say where a live VRAM tile is in
the ROM - raw (the kart sheets are stored in place) or inside one of the
compressed streams some caller decompresses (tools/labs/decompsites.py)."""
import sys, os
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, ".."))
from smktool.compress import decompress
from decompsites import sites


def sprites(b):
    """[(i, x, y, char9, pal, size, hflip, vflip)] for every on-screen OAM entry"""
    out = []
    o = b.oam
    for k in range(128):
        y = o[k * 4 + 1]
        if y in (0, 0xF0, 0xE0):
            continue
        x, t, a = o[k * 4], o[k * 4 + 2], o[k * 4 + 3]
        hi = o[512 + (k >> 2)]
        big = (hi >> ((k & 3) * 2 + 1)) & 1
        x |= ((hi >> ((k & 3) * 2)) & 1) << 8
        t |= (a & 1) << 8
        if x < 256 and y < 224:
            out.append((k, x, y, t, (a >> 1) & 7, big, (a >> 6) & 1, (a >> 7) & 1))
    return out


def char_tiles(char9, size):
    """the VRAM tile numbers an OAM entry draws: OBJ base $4000 words, name
    select 1, so table 0 is $400+c and table 1 is $600+c (NOTES 272)"""
    base = 0x600 if char9 & 0x100 else 0x400
    c = char9 & 0xFF
    n = 2 if size else 1
    out = []
    for ty in range(n):
        for tx in range(n):
            out.append(base + (((c & 0xF0) + ((c + tx) & 0x0F) + ty * 16) & 0xFF))
    return out


class Locator:
    def __init__(self, rom):
        self.rom = rom
        self.data = bytes(rom.data)
        self.raw = {}
        for pc in range(0, len(self.data) - 32, 32):
            self.raw.setdefault(self.data[pc:pc + 32], pc)
        self.streams = {}
        # the per-theme entity sheets are table-driven (SMK_OBJ_TABLE at
        # $81:EBD3, three bytes a theme, NOTES 272), so no static call
        # site names them - add them by hand
        tp = rom.snes_to_pc(0x81EBD3)
        themed = [self.data[tp + t * 3] | (self.data[tp + t * 3 + 1] << 8) | (self.data[tp + t * 3 + 2] << 16)
                  for t in range(8)]
        themed_set = set(themed); self.themed_set = themed_set
        for src in themed:
            try:
                out, _ = decompress(self.data, rom.snes_to_pc(src), max_out=0x20000)
                self.streams[src] = bytes(out)
            except Exception:
                pass
        for _c, src, _d, _w in sites(self.data, rom):
            if (src >> 16) == 0x7F or src in self.streams:
                continue
            try:
                out, _ = decompress(self.data, rom.snes_to_pc(src), max_out=0x20000)
            except Exception:
                continue
            self.streams[src] = bytes(out)

    def where(self, tile32):
        """'ROM $C0:7B20 (sheet $C0:2000 tile 733)' or 'stream $C1:0000 tile 13' or None"""
        if not any(tile32):
            return "blank"
        pc = self.raw.get(tile32)
        if pc is not None:
            snes = self.rom.pc_to_snes(pc)
            bank_base = (snes & 0xFF0000) | 0x2000
            if 0x2000 <= (snes & 0xFFFF) < 0x8000:
                t = ((snes & 0xFFFF) - 0x2000) // 32
                return "ROM $%06X (sheet $%06X tile %d)" % (snes, bank_base, t)
            return "ROM $%06X" % snes
        for src, buf in self.streams.items():
            i = buf.find(tile32)
            if i >= 0 and i % 32 == 0:
                return "stream $%06X tile %d%s" % (src, i // 32,
                       "  (theme sheet: VRAM $%03X)" % (0x4C0 + i // 32) if src in self.themed_set else "")
        return None
