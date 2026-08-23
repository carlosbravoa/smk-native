"""ROM container: mapping, header, checksum, identification."""
from __future__ import annotations
import hashlib, struct, os
from dataclasses import dataclass

# --- Known-good base ROMs -------------------------------------------------
# We record hashes only (a hash is not the work). Never commit a ROM.
KNOWN = {
    "47e103d8398cf5b7cbb42b95df3a3c270691163b": {
        "name": "Super Mario Kart (USA)", "region": "USA",
        "size": 0x80000, "mapper": "hirom", "coprocessor": "DSP-1",
    },
    "0b4b6ce3b5f1e9f3e4a7f0b9e1c0d2a3b4c5d6e7": {  # placeholder, filled on demand
        "name": "(unknown)", "region": "?", "size": 0, "mapper": "hirom",
    },
}


class MappingError(ValueError):
    pass


@dataclass
class Header:
    title: str
    mapmode: int
    cart_type: int
    rom_size_log: int
    sram_size_log: int
    country: int
    dev_id: int
    version: int
    checksum: int
    complement: int
    base: int          # file offset of the $FFC0-style header

    @property
    def fastrom(self) -> bool:
        return bool(self.mapmode & 0x10)

    @property
    def mapper(self) -> str:
        return {0x0: "lorom", 0x1: "hirom", 0x5: "exhirom"}.get(self.mapmode & 0xF, "unknown")


class Rom:
    """A SNES ROM image with HiROM address mapping helpers."""

    def __init__(self, data: bytes | bytearray, path: str | None = None):
        if len(data) % 1024 == 512:           # copier header
            data = data[512:]
        self.data = bytearray(data)
        self.path = path
        self.header = self._parse_header()
        if self.header.mapper != "hirom":
            raise MappingError(
                f"smktool currently targets HiROM; got {self.header.mapper}")

    # ---- construction ----
    @classmethod
    def load(cls, path: str) -> "Rom":
        with open(path, "rb") as f:
            return cls(f.read(), path)

    def save(self, path: str) -> None:
        with open(path, "wb") as f:
            f.write(self.data)

    # ---- identity ----
    @property
    def sha1(self) -> str:
        return hashlib.sha1(self.data).hexdigest()

    @property
    def md5(self) -> str:
        return hashlib.md5(self.data).hexdigest()

    def identify(self) -> dict:
        info = dict(KNOWN.get(self.sha1, {"name": "unrecognised", "region": "?"}))
        info["sha1"] = self.sha1
        info["size"] = len(self.data)
        info["title"] = self.header.title
        info["mapper"] = self.header.mapper
        info["fastrom"] = self.header.fastrom
        info["known"] = self.sha1 in KNOWN
        return info

    # ---- header ----
    def _parse_header(self) -> Header:
        best, best_score = None, -1
        for base in (0xFFC0, 0x7FC0, 0x40FFC0):
            if base + 0x40 > len(self.data):
                continue
            h = self.data[base:base + 0x40]
            title = h[0:21].decode("latin1")
            score = sum(1 for c in title if 32 <= ord(c) < 127)
            csc, cs = struct.unpack("<HH", h[0x1C:0x20])
            if (csc ^ cs) == 0xFFFF:
                score += 32
            if score > best_score:
                best_score, best = score, (base, h, title, csc, cs)
        base, h, title, csc, cs = best
        return Header(title=title.rstrip(), mapmode=h[0x15], cart_type=h[0x16],
                      rom_size_log=h[0x17], sram_size_log=h[0x18], country=h[0x19],
                      dev_id=h[0x1A], version=h[0x1B], checksum=cs, complement=csc,
                      base=base)

    # ---- checksum ----
    def compute_checksum(self) -> int:
        """SNES checksum: sum of all bytes, with the header's own checksum
        fields treated as $00/$FF... in practice, sum every byte after writing
        complement=$FFFF^sum. We use the standard mirror-aware sum."""
        n = len(self.data)
        if n & (n - 1) == 0:                 # power of two: plain sum
            return sum(self.data) & 0xFFFF
        # non power-of-two: sum lower power-of-two part + mirrored remainder
        p = 1 << (n.bit_length() - 1)
        s = sum(self.data[:p])
        rest = self.data[p:]
        reps = p // len(rest) if rest else 0
        return (s + sum(rest) * reps) & 0xFFFF

    def fix_checksum(self) -> tuple[int, int]:
        """Recompute and write the checksum pair. Returns (old, new)."""
        b = self.header.base
        old = struct.unpack("<H", bytes(self.data[b + 0x1E:b + 0x20]))[0]
        # zero the fields, then sum: complement=$0000 sum, checksum=$0000
        self.data[b + 0x1C:b + 0x1E] = b"\xFF\xFF"
        self.data[b + 0x1E:b + 0x20] = b"\x00\x00"
        s = self.compute_checksum()
        self.data[b + 0x1C:b + 0x1E] = struct.pack("<H", s ^ 0xFFFF)
        self.data[b + 0x1E:b + 0x20] = struct.pack("<H", s)
        self.header = self._parse_header()
        return old, s

    def checksum_ok(self) -> bool:
        b = self.header.base
        saved_c = bytes(self.data[b + 0x1C:b + 0x1E])
        saved_s = bytes(self.data[b + 0x1E:b + 0x20])
        self.data[b + 0x1C:b + 0x1E] = b"\xFF\xFF"
        self.data[b + 0x1E:b + 0x20] = b"\x00\x00"
        s = self.compute_checksum()
        self.data[b + 0x1C:b + 0x1E] = saved_c
        self.data[b + 0x1E:b + 0x20] = saved_s
        return struct.unpack("<H", saved_s)[0] == s

    # ---- address mapping (HiROM) ----
    @property
    def mask(self) -> int:
        return len(self.data) - 1

    def snes_to_pc(self, addr: int) -> int:
        """$BB:AAAA -> file offset. Raises for non-ROM addresses."""
        bank, off = (addr >> 16) & 0xFF, addr & 0xFFFF
        b = bank & 0x7F
        if b <= 0x3F and off < 0x8000:
            raise MappingError(f"${addr:06X} is system/WRAM area, not ROM")
        if 0x7E <= bank <= 0x7F:
            raise MappingError(f"${addr:06X} is WRAM")
        return ((bank & 0x3F) << 16 | off) & self.mask

    def pc_to_snes(self, pc: int, prefer_fast: bool = True) -> int:
        """file offset -> canonical $BB:AAAA. Uses $80-$BF for the $8000-$FFFF
        halves (as the game's own code does) and $C0-$FF otherwise."""
        if not 0 <= pc < len(self.data):
            raise MappingError(f"pc 0x{pc:X} out of range")
        bank, off = pc >> 16, pc & 0xFFFF
        hi = 0x80 if (off >= 0x8000 and prefer_fast) else 0xC0
        return (hi | bank) << 16 | off

    # ---- reads ----
    def u8(self, pc: int) -> int:
        return self.data[pc]

    def u16(self, pc: int) -> int:
        return self.data[pc] | self.data[pc + 1] << 8

    def u24(self, pc: int) -> int:
        return self.data[pc] | self.data[pc + 1] << 8 | self.data[pc + 2] << 16

    def read(self, pc: int, n: int) -> bytes:
        return bytes(self.data[pc:pc + n])

    def vectors(self) -> dict[str, int]:
        b = self.header.base
        n = lambda o: struct.unpack("<H", bytes(self.data[b + o:b + o + 2]))[0]
        return {
            "native.COP": n(0x24), "native.BRK": n(0x26), "native.ABORT": n(0x28),
            "native.NMI": n(0x2A), "native.IRQ": n(0x2E),
            "emu.COP": n(0x34), "emu.ABORT": n(0x38), "emu.NMI": n(0x3A),
            "emu.RESET": n(0x3C), "emu.IRQ": n(0x3E),
        }
