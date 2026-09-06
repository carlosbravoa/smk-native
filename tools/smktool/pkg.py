"""Course packages: the Python twin of src/tracks.c (docs/TRACKS.md 2).

A package directory holds course.txt, map.bin, sectors.bin, line.txt and
optionally style/.  `from_rom` assembles one from a ROM slot, exactly the
fields src/course.c and src/assets.c read; the C selftest round-trips
the twenty GP courses through the C writer and reader, and this module
writes the same files so the tools and the game agree.
"""
from __future__ import annotations
import os
from dataclasses import dataclass, field
from .rom import Rom
from . import course as C, mode7 as M, surface as S

SEG_OFF_DEFAULT = (0, 8, 16, 24, 0, 0, 0, 0)


@dataclass
class Package:
    id: str = ""
    name: str = ""
    theme: int = 0
    rom_track: int = -1
    map: bytes = b""                       # 16384 tile indices, before stamping
    stamps: list = field(default_factory=list)     # (kind, col, row)
    ents: list = field(default_factory=list)       # (col, row, kindbits)
    sect: bytes = b""                      # 4096 cells, $7F unpainted, no finish bit
    line: list = field(default_factory=list)       # (x, y, attr) in pixels
    lap_word: int = 0
    finish: tuple = (0, 0, 0, 0)           # cell x, cell y, w, h
    grid: tuple = (0, 0, 0)                # x, y, step
    segments: list = field(default_factory=list)   # thresholds, $FF excluded
    seg_off: tuple = SEG_OFF_DEFAULT
    items: int = 1
    music: str = ""
    style: dict | None = None              # {"tiles","palette","surface"} bytes

    @property
    def sectors(self) -> int:
        return len(self.line)


def from_rom(rom: Rom, track: int) -> Package:
    p = Package()
    p.rom_track = track
    p.id = "rom%02d" % track
    p.theme = S.theme_of(rom, track)
    p.map = M.tilemap(rom, track)
    p.stamps = [(k, x // 8, y // 8) for k, x, y in C.objects(rom, track)]
    m, n = C.build_sector_map(rom, track)
    p.sect = bytes(m)
    p.line = [(x, y, a) for x, y, a in C.waypoints(rom, track, n)[:n]]
    fin = C.mark_finish(rom, track, bytearray(m))
    p.lap_word = fin["lap_word"]
    p.finish = (fin["cell"] % 64, fin["cell"] // 64, fin["w"], fin["h"])
    e = rom.snes_to_pc(0x818A79) + track * 2
    p4 = rom.snes_to_pc(0x810000 | rom.u16(e))
    g = rom.snes_to_pc(0x810000 | rom.u16(p4))
    sx = lambda v: v - 65536 if v > 32767 else v
    p.grid = (sx(rom.u16(g + 2)), sx(rom.u16(g + 4)), sx(rom.u16(g + 6)))
    d28 = rom.data[rom.snes_to_pc(0x818B73) + track]
    d2c = rom.data[rom.snes_to_pc(0x818B8C) + track]
    p.items = d28 >> 1 if track < 20 else 1
    p.segments = []
    st = rom.u16(rom.snes_to_pc(0x84DB83) + d28)
    if st:
        lst = rom.u16(rom.snes_to_pc(0x840000 | st) + d2c)
        if lst:
            tp = rom.snes_to_pc(0x840000 | lst)
            for i in range(8):
                v = rom.data[tp + i]
                if v == 0xFF:
                    break
                p.segments.append(v)
    p.seg_off = tuple(rom.u16(rom.snes_to_pc(0x84DAC5) + i * 2) for i in range(8))
    p3 = rom.snes_to_pc(0x85C800) + track * 64
    for i in range(32):
        wd = rom.u16(p3 + i * 2)
        if wd == 0:
            break
        p.ents.append((wd & 0x7F, (wd >> 7) & 0x7F, wd >> 14))
    return p


def write(p: Package, d: str) -> None:
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "course.txt"), "w") as f:
        f.write("# smk-port course package (docs/TRACKS.md)\n")
        f.write("format   1\n")
        f.write("name     %s\n" % (p.name or p.id.upper()))
        f.write("theme    %d\n" % p.theme)
        if p.music:
            f.write("music    %s\n" % p.music)
        f.write("items    %d\n" % p.items)
        if p.lap_word:
            f.write("lapword  0x%04X\n" % p.lap_word)
        f.write("grid     %d %d %d\n" % p.grid)
        f.write("finish   %d %d %d %d\n" % p.finish)
        if p.style:
            f.write("style    style/\n")
        f.write("# stamps: the ROM's kind byte, then the tile column and row\n")
        for k, x, y in p.stamps:
            f.write("object   0x%02X %d %d\n" % (k, x, y))
        f.write("# sprite obstacles: tile column, row, and the word's kind bits\n")
        for x, y, k in p.ents:
            f.write("entity   %d %d %d\n" % (x, y, k))
        f.write("# entity spawn windows open at these sector indices\n")
        for s in p.segments:
            f.write("segment  %d\n" % s)
        if tuple(p.seg_off) != SEG_OFF_DEFAULT:
            f.write("segoff   " + " ".join(str(v) for v in p.seg_off) + "\n")
    with open(os.path.join(d, "map.bin"), "wb") as f:
        f.write(p.map)
    with open(os.path.join(d, "sectors.bin"), "wb") as f:
        f.write(p.sect)
    with open(os.path.join(d, "line.txt"), "w") as f:
        f.write("# one waypoint per sector: x y attr (pixels; attr bits 0-1 the AI speed row, bit 7 airborne-reject)\n")
        for x, y, a in p.line:
            f.write("%4d %4d %d\n" % (x, y, a))
    if p.style:
        sd = os.path.join(d, "style")
        os.makedirs(sd, exist_ok=True)
        for k in ("tiles", "palette", "surface"):
            with open(os.path.join(sd, k + ".bin"), "wb") as f:
                f.write(p.style[k])


def read(d: str) -> Package:
    p = Package()
    p.id = os.path.basename(os.path.normpath(d))
    p.name = p.id.upper()
    path = os.path.join(d, "course.txt")
    with open(path) as f:
        for ln, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            key, _, v = line.partition(" ")
            v = v.strip()
            a = v.split()
            if key == "format":
                if int(v) != 1:
                    raise ValueError("%s:%d: format %s" % (path, ln, v))
            elif key == "name":
                p.name = v
            elif key == "theme":
                p.theme = int(v)
            elif key == "music":
                p.music = v
            elif key == "items":
                p.items = int(v)
            elif key == "lapword":
                p.lap_word = int(v, 0)
            elif key == "grid":
                p.grid = tuple(int(t) for t in a[:3])
            elif key == "finish":
                p.finish = tuple(int(t) for t in a[:4])
            elif key == "object":
                p.stamps.append((int(a[0], 0), int(a[1]), int(a[2])))
            elif key == "entity":
                p.ents.append((int(a[0]), int(a[1]), int(a[2]) if len(a) > 2 else 0))
            elif key == "segment":
                p.segments.append(int(v))
            elif key == "segoff":
                p.seg_off = tuple(int(t) for t in a[:8])
            elif key == "style":
                p.style = {}
            else:
                raise ValueError("%s:%d: unknown key %s" % (path, ln, key))
    p.map = open(os.path.join(d, "map.bin"), "rb").read()
    sp = os.path.join(d, "sectors.bin")
    p.sect = open(sp, "rb").read() if os.path.exists(sp) else b""
    lp = os.path.join(d, "line.txt")
    if os.path.exists(lp):
        for raw in open(lp):
            line = raw.split("#", 1)[0].split()
            if len(line) >= 2:
                p.line.append((int(line[0]), int(line[1]), int(line[2], 0) if len(line) > 2 else 0))
    if p.style is not None:
        sd = os.path.join(d, "style")
        p.style = {k: open(os.path.join(sd, k + ".bin"), "rb").read() for k in ("tiles", "palette", "surface")}
    return p
