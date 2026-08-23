"""Mode 7 track assets: tilemaps, tile expansion, rendering.

The three pieces the game uploads for a track, and where they come from:

  tilemap   128x128 tile indices -> VRAM low bytes  (DMA at $81E7B5, $4000 bytes)
            pointer table $81EB5B, 24 entries, DOUBLY compressed
  tiles     192 8x8 Mode 7 tiles -> VRAM high bytes (DMA at $81E769, $3000 bytes)
            expanded from a packed blob by the routine at $84E3C7
  palette   256 BGR555 colours, pointer table $81EBBB
"""
from __future__ import annotations
from .compress import decompress
from .rom import Rom

TILEMAP_TABLE = 0x81EB5B     # 24 entries: 20 GP tracks + 4 battle courses
TILESET_TABLE = 0x81EBA3
PALETTE_TABLE = 0x81EBBB
TILEMAP_DIM = 128            # tiles per side
TILE_PX = 8
DEFAULT_TILES = 192          # the count $81E6F0 passes to the expander


def pointer(rom: Rom, table: int, index: int) -> int:
    pc = rom.snes_to_pc(table) + index * 3
    return (rom.data[pc + 2] << 16) | rom.u16(pc)


def _blob(rom: Rom, table: int, index: int, twice: bool = False) -> bytes:
    d, _ = decompress(bytes(rom.data), rom.snes_to_pc(pointer(rom, table, index)))
    if twice:
        d, _ = decompress(bytes(d), 0)
    return bytes(d)


def tilemap(rom: Rom, track: int) -> bytes:
    """128x128 tile indices. Compressed twice - the first pass produces
    another stream, not the map."""
    m = _blob(rom, TILEMAP_TABLE, track, twice=True)
    if len(m) != TILEMAP_DIM * TILEMAP_DIM:
        raise ValueError(f"track {track}: expected 16384 bytes, got {len(m)}")
    return m


def palette(rom: Rom, index: int) -> bytes:
    return _blob(rom, PALETTE_TABLE, index)


def expand_tiles(packed: bytes, count: int = DEFAULT_TILES) -> bytes:
    """Reimplementation of the tile expander at $84E3C7.

    Source layout:
        +$000   one palette-base byte per tile (256 bytes)
        +$100   32 bytes per tile: two 4-bit pixels per byte, LOW nibble first

    Each nibble becomes one 8-bit Mode 7 pixel; a non-zero nibble is OR-ed
    with that tile's palette base, so one 16-colour tile can land anywhere in
    the 256-colour Mode 7 palette.  Zero stays zero - it is the transparent /
    backdrop index and must not be shifted.
    """
    out = bytearray(count * 64)
    need = 0x100 + count * 32
    if len(packed) < need:
        raise ValueError(f"packed tileset is {len(packed)} bytes, need {need} "
                         f"for {count} tiles")
    o = 0
    for t in range(count):
        base = packed[t]
        src = 0x100 + t * 32
        for k in range(32):
            b = packed[src + k]
            lo, hi = b & 0x0F, b >> 4
            out[o] = (lo | base) if lo else 0
            out[o + 1] = (hi | base) if hi else 0
            o += 2
    return bytes(out)


def tileset(rom: Rom, index: int, count: int = DEFAULT_TILES) -> bytes:
    return expand_tiles(_blob(rom, TILESET_TABLE, index), count)


def render_track(tmap: bytes, tiles: bytes, pal: list) -> tuple[int, int, bytes]:
    """Full 1024x1024 track surface as RGB rows."""
    n = TILEMAP_DIM
    w = h = n * TILE_PX
    buf = bytearray(w * h * 3)
    ntiles = len(tiles) // 64
    for ty in range(n):
        for tx in range(n):
            t = tmap[ty * n + tx]
            if t >= ntiles:
                continue
            src = t * 64
            for py in range(8):
                row = (ty * 8 + py) * w + tx * 8
                for px in range(8):
                    c = pal[tiles[src + py * 8 + px]]
                    p = (row + px) * 3
                    buf[p] = c[0]; buf[p + 1] = c[1]; buf[p + 2] = c[2]
    return w, h, bytes(buf)
