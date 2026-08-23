"""Compressed-asset pointer tables: export, edit, re-import.

An asset is reached through a 3-byte pointer (16-bit address + bank) in one
of the tables at $81EB5B..  Editing one means decompressing it, changing the
bytes, re-compressing, and putting it back - either in its original slot if
the new stream fits, or in free space with the pointer updated.
"""
from __future__ import annotations
import json, os
from dataclasses import dataclass, asdict

from .rom import Rom, MappingError
from .compress import decompress, compress, stream_size


@dataclass
class Entry:
    table: int          # snes address of the pointer table
    index: int
    src: int            # snes address of the compressed stream
    comp_len: int       # bytes the stream occupies in ROM
    size: int           # decompressed size


class PointerTable:
    STRIDE = 3

    def __init__(self, rom: Rom, addr: int, count: int, name: str = ""):
        self.rom, self.addr, self.count, self.name = rom, addr, count, name

    def _slot(self, index: int) -> int:
        return self.rom.snes_to_pc(self.addr) + index * self.STRIDE

    def pointer(self, index: int) -> int:
        p = self._slot(index)
        return (self.rom.data[p + 2] << 16) | self.rom.u16(p)

    def set_pointer(self, index: int, snes: int) -> None:
        p = self._slot(index)
        self.rom.data[p] = snes & 0xFF
        self.rom.data[p + 1] = (snes >> 8) & 0xFF
        self.rom.data[p + 2] = (snes >> 16) & 0xFF

    def entry(self, index: int) -> Entry | None:
        src = self.pointer(index)
        try:
            off = self.rom.snes_to_pc(src)
        except MappingError:
            return None
        r = stream_size(bytes(self.rom.data), off)
        if r is None:
            return None
        return Entry(self.addr, index, src, r[1], r[0])

    def read(self, index: int) -> bytes:
        off = self.rom.snes_to_pc(self.pointer(index))
        return bytes(decompress(bytes(self.rom.data), off)[0])

    def entries(self) -> list[Entry]:
        out = []
        for i in range(self.count):
            e = self.entry(i)
            if e:
                out.append(e)
        return out


# ---------------------------------------------------------------------------
def free_runs(rom: Rom, min_len: int = 32) -> list[tuple[int, int, int]]:
    """Runs of uniform $00 or $FF filler, as (file_offset, length, fill)."""
    D = rom.data
    runs, start, fill = [], None, None
    for i, b in enumerate(D):
        if b in (0x00, 0xFF) and (fill is None or b == fill):
            if start is None:
                start, fill = i, b
        else:
            if start is not None and i - start >= min_len:
                runs.append((start, i - start, fill))
            start, fill = None, None
    if start is not None and len(D) - start >= min_len:
        runs.append((start, len(D) - start, fill))
    return runs


class FreeSpace:
    """Hands out ROM space for relocated assets, never crossing a bank."""

    def __init__(self, rom: Rom, min_len: int = 32, reserve: int = 0):
        self.rom = rom
        self.runs = [list(r) for r in free_runs(rom, min_len)]
        self.runs.sort(key=lambda r: -r[1])
        self.used = 0

    def alloc(self, n: int) -> int:
        """Return a file offset with `n` free bytes, not crossing a bank."""
        for run in self.runs:
            start, length, _ = run
            # keep the whole block inside one 64 KB bank
            bank_end = (start | 0xFFFF) + 1
            avail = min(start + length, bank_end) - start
            if avail >= n:
                run[0] += n
                run[1] -= n
                self.used += n
                return start
        raise MemoryError(
            f"no free run of {n} bytes; expand the ROM (`smk expand`) "
            "or free space by hand")


def repack(rom: Rom, table: PointerTable, index: int, data: bytes,
           free: FreeSpace | None = None) -> dict:
    """Compress `data` and store it, re-pointing the table if it must move."""
    e = table.entry(index)
    if e is None:
        raise ValueError(f"{table.name}[{index}] does not resolve to a stream")
    enc = bytes(compress(data))
    # sanity: the game must be able to read back exactly what we put in
    back, _ = decompress(enc, 0)
    if bytes(back) != data:
        raise AssertionError("compressor round-trip failed - refusing to write")

    off = rom.snes_to_pc(e.src)
    if len(enc) <= e.comp_len:
        rom.data[off:off + len(enc)] = enc
        # leave the tail untouched; it is unreachable once $FF is hit
        return {"index": index, "action": "in-place", "was": e.comp_len,
                "now": len(enc), "src": e.src}

    if free is None:
        raise MemoryError(
            f"{table.name}[{index}] grew {e.comp_len} -> {len(enc)} bytes "
            "and no free space pool was supplied")
    new_off = free.alloc(len(enc))
    rom.data[new_off:new_off + len(enc)] = enc
    new_snes = rom.pc_to_snes(new_off)
    table.set_pointer(index, new_snes)
    return {"index": index, "action": "relocated", "was": e.comp_len,
            "now": len(enc), "src": e.src, "new_src": new_snes}


# ---------------------------------------------------------------------------
def expand(rom: Rom, size: int) -> None:
    """Grow a HiROM image, padding with $00 and fixing the size byte.

    SMK is a very full 512 KB cart (~18 KB of filler), so any substantial hack
    needs the extra banks.  HiROM addresses banks $C0-$FF, so 1 MB and 2 MB
    images both work in emulators and on flash carts.
    """
    if size & (size - 1):
        raise ValueError("ROM size must be a power of two")
    if size < len(rom.data):
        raise ValueError("refusing to shrink the ROM")
    if size == len(rom.data):
        return
    rom.data.extend(b"\x00" * (size - len(rom.data)))
    # header byte $D7 is log2(size in KB): $09 = 512 KB, $0A = 1 MB, $0B = 2 MB
    log = size.bit_length() - 11
    b = rom.header.base
    rom.data[b + 0x17] = log
    rom.header = rom._parse_header()


# ---------------------------------------------------------------------------
# Known pointer tables.
#
# Boundaries come from the code that indexes them (each is loaded with
# `lda.l TABLE,x` where x = index*3); counts are how many entries still
# resolve to a decodable stream.  Contents are described only where the data
# itself settled the question.
REGISTRY = {
    "gfx_a":    (0x81EB5B, 24, "large graphics, 1.5-7.6 KB each"),
    "gfx_b":    (0x81EBA3,  8, "graphics, 1.5-6.4 KB each"),
    "palette":  (0x81EBBB,  8, "256-colour BGR555 palettes, 512 bytes each"),
    "gfx_d":    (0x81EBEB,  8, "small graphics, 0.3-1.8 KB each"),
    "gfx_e":    (0x81EC03,  8, "graphics, 1536 bytes each"),
    "gfx_f":    (0x81F47E, 14, "graphics, 0.5-1.4 KB each"),
}


def table(rom: Rom, name: str) -> PointerTable:
    if name not in REGISTRY:
        raise KeyError(f"unknown table {name!r}; known: {', '.join(REGISTRY)}")
    addr, count, _ = REGISTRY[name]
    return PointerTable(rom, addr, count, name)


def manifest(rom: Rom) -> dict:
    out = {}
    for name in REGISTRY:
        t = table(rom, name)
        out[name] = {
            "addr": "$%06X" % t.addr,
            "description": REGISTRY[name][2],
            "entries": [
                {"index": e.index, "src": "$%06X" % e.src,
                 "compressed": e.comp_len, "size": e.size}
                for e in t.entries()
            ],
        }
    return out
