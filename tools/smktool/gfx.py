"""SNES tile decoding and PNG output (no third-party dependencies)."""
from __future__ import annotations
import struct, zlib

# SNES planar tile formats: each 8x8 tile is stored as bitplane pairs.
BPP_SIZES = {2: 16, 4: 32, 8: 64}


def decode_tile(data: bytes, off: int, bpp: int) -> list[list[int]]:
    """One 8x8 tile -> 8 rows of 8 palette indices."""
    rows = [[0] * 8 for _ in range(8)]
    for pair in range(bpp // 2):
        base = off + pair * 16
        for y in range(8):
            lo = data[base + y * 2]
            hi = data[base + y * 2 + 1]
            for x in range(8):
                bit = 7 - x
                v = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1)
                rows[y][x] |= v << (pair * 2)
    return rows


def decode_mode7_tile(data: bytes, off: int) -> list[list[int]]:
    """Mode 7 tiles are NOT planar: 64 bytes, row-major, one byte per pixel."""
    return [list(data[off + y * 8: off + y * 8 + 8]) for y in range(8)]


def decode_tiles(data: bytes, bpp: int = 4, count: int | None = None,
                 mode7: bool = False) -> list[list[list[int]]]:
    if mode7:
        n = len(data) // 64 if count is None else count
        return [decode_mode7_tile(data, i * 64) for i in range(n)]
    size = BPP_SIZES[bpp]
    n = len(data) // size if count is None else count
    return [decode_tile(data, i * size, bpp) for i in range(n)]


def bgr555(v: int) -> tuple[int, int, int]:
    """SNES colour word -> 8-bit RGB (5-bit channels scaled, not just <<3)."""
    r, g, b = v & 0x1F, (v >> 5) & 0x1F, (v >> 10) & 0x1F
    f = lambda c: (c * 255 + 15) // 31
    return f(r), f(g), f(b)


def read_palette(data: bytes, off: int, colors: int = 16) -> list[tuple[int, int, int]]:
    return [bgr555(data[off + i * 2] | data[off + i * 2 + 1] << 8)
            for i in range(colors)]


def grey_palette(n: int = 16) -> list[tuple[int, int, int]]:
    return [(i * 255 // (n - 1),) * 3 for i in range(n)]


def tilesheet(tiles: list, palette: list[tuple[int, int, int]],
              per_row: int = 16) -> tuple[int, int, bytes]:
    """Lay tiles out in a grid -> (width, height, RGB rows)."""
    rows_of = (len(tiles) + per_row - 1) // per_row
    w, h = per_row * 8, rows_of * 8
    buf = bytearray(w * h * 3)
    for idx, t in enumerate(tiles):
        tx, ty = (idx % per_row) * 8, (idx // per_row) * 8
        for y in range(8):
            for x in range(8):
                c = palette[t[y][x] % len(palette)]
                p = ((ty + y) * w + tx + x) * 3
                buf[p:p + 3] = bytes(c)
    return w, h, bytes(buf)


def write_png(path: str, w: int, h: int, rgb: bytes, scale: int = 1) -> None:
    if scale > 1:
        big = bytearray(w * scale * h * scale * 3)
        for y in range(h):
            row = rgb[y * w * 3:(y + 1) * w * 3]
            wide = bytearray()
            for x in range(w):
                wide += row[x * 3:x * 3 + 3] * scale
            for s in range(scale):
                o = ((y * scale + s) * w * scale) * 3
                big[o:o + len(wide)] = wide
        w, h, rgb = w * scale, h * scale, bytes(big)

    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * w * 3:(y + 1) * w * 3]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


# ---------------------------------------------------------------------------
# Format identification
#
# A blob's layout is not recorded anywhere in the ROM - the routine that
# uploads it knows.  Rather than guess, score each candidate decoding: real
# tile art has low local variation inside a tile (flat runs, smooth edges),
# while a wrong format shreds bytes across pixels and looks like noise.

def coherence(tiles: list, levels: int = 255) -> float:
    """Lower is better: mean absolute step between neighbouring pixels,
    expressed as a fraction of the format's full range.

    Normalising by `levels` is essential - a 2bpp decoding only has four
    possible values, so its raw steps are small no matter how wrong it is.
    """
    if not tiles:
        return 1e9
    total = 0
    n = 0
    for t in tiles:
        for row in t:
            for x in range(7):
                total += abs(row[x] - row[x + 1])
                n += 1
        for y in range(7):
            for x in range(8):
                total += abs(t[y][x] - t[y + 1][x])
                n += 1
    return total / max(n, 1) / levels


def deinterleave(data: bytes, phase: int) -> bytes:
    return data[phase::2]


CANDIDATE_FORMATS = [
    #  name                decode kwargs      deinterleave phase   levels
    ("mode7",            dict(mode7=True),  None, 255),
    ("4bpp",             dict(bpp=4),       None, 15),
    ("2bpp",             dict(bpp=2),       None, 3),
    ("mode7/deint-even", dict(mode7=True),  0,    255),
    ("mode7/deint-odd",  dict(mode7=True),  1,    255),
    ("4bpp/deint-even",  dict(bpp=4),       0,    15),
    ("4bpp/deint-odd",   dict(bpp=4),       1,    15),
    ("2bpp/deint-even",  dict(bpp=2),       0,    3),
    ("2bpp/deint-odd",   dict(bpp=2),       1,    3),
]


def identify_format(data: bytes, max_tiles: int = 128) -> list[tuple[str, float, int]]:
    """Score every candidate layout.  Returns (name, score, tile_count),
    best first."""
    results = []
    for name, kw, phase, levels in CANDIDATE_FORMATS:
        buf = deinterleave(data, phase) if phase is not None else data
        try:
            tiles = decode_tiles(buf, **kw)
        except Exception:
            continue
        if not tiles:
            continue
        results.append((name, coherence(tiles[:max_tiles], levels), len(tiles)))
    results.sort(key=lambda r: r[1])
    return results


# ---------------------------------------------------------------------------
# Sprite sheets
#
# Kart sprites are uncompressed 4bpp stored in PPU order: a 32x32 sprite is
# 4x4 tiles with a 16-tile row stride, and frames advance four tiles across
# then sixty-four tiles down.  See docs/NOTES.md 028.

SPRITE_PX = 32
SPRITE_TILES = 4
SPRITE_ROW_STRIDE = 16


def sprite_frame(data: bytes, base: int, frame: int) -> list[list[int]]:
    """One 32x32 frame as rows of palette indices."""
    out = [[0] * SPRITE_PX for _ in range(SPRITE_PX)]
    n0 = (frame % 4) * SPRITE_TILES + (frame // 4) * (SPRITE_ROW_STRIDE * 4)
    for tr in range(SPRITE_TILES):
        for tc in range(SPRITE_TILES):
            off = base + (n0 + tr * SPRITE_ROW_STRIDE + tc) * 32
            if off + 32 > len(data):
                continue
            px = decode_tile(data, off, 4)
            for y in range(8):
                for x in range(8):
                    out[tr * 8 + y][tc * 8 + x] = px[y][x]
    return out


def sprite_sheet(data: bytes, base: int, frames: int,
                 palette: list, per_row: int = 8,
                 bg: tuple = (40, 40, 60)) -> tuple[int, int, bytes]:
    rows = (frames + per_row - 1) // per_row
    w, h = per_row * SPRITE_PX, rows * SPRITE_PX
    buf = bytearray(bytes(bg) * (w * h))
    for f in range(frames):
        px = sprite_frame(data, base, f)
        fx, fy = (f % per_row) * SPRITE_PX, (f // per_row) * SPRITE_PX
        for y in range(SPRITE_PX):
            for x in range(SPRITE_PX):
                v = px[y][x]
                if v == 0:
                    continue
                c = palette[v % len(palette)]
                p = ((fy + y) * w + fx + x) * 3
                buf[p:p + 3] = bytes(c)
    return w, h, bytes(buf)
