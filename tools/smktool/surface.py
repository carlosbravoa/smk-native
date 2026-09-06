"""The surface class of every Mode 7 tile, per theme.

$81EB11: decompress $87:FDBA, copy 192 bytes from $81:EB4B[theme] into
RAM $0B00 - one class byte per tile.  Tiles 192-255 (the object band:
boxes, coins, pads, ramps, oil) have their classes CAPTURED from the
running game's $0BC0 (NOTES 069); the code filling them is undecoded,
so the same 64 bytes src/assets.c carries are carried here.

Class families (src/player.c, docs/TRACKS.md 4.2):
  $00-$1E  object band: $10 ramp, $12/$1C mud jump, $14 item box,
           $16 boost pad, $18 oil, $1A coin, $1E small bump
  $20-$3E  hazards: $20 void, $22 wade, $24 lava, $26 deep, $28 edge fall
  $40-$7E  driveable; the type (s>>1)&15 picks the cap and drag rows
  $80+     solid; $82/$84 breakable
"""
from __future__ import annotations
from .compress import decompress
from .rom import Rom

SURF_BLOB = 0x87FDBA
TBL_SURF_OFF = 0x81EB4B
TBL_THEME = 0x81EC2F

OBJ_SURF = bytes([
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16,
    0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16, 0x16,
    0x80, 0x80, 0x80, 0x80, 0x10, 0x10, 0x10, 0x10,
    0x10, 0x10, 0x18, 0x18, 0x18, 0x18, 0x1A, 0x40,
])

_cache: dict[int, bytes] = {}


def theme_of(rom: Rom, track: int) -> int:
    return rom.data[rom.snes_to_pc(TBL_THEME) + track] >> 1


def table(rom: Rom, theme: int) -> bytes:
    """256 class bytes: the theme's 192 and the captured object band."""
    key = id(rom) ^ theme
    if key in _cache:
        return _cache[key]
    blob, _ = decompress(bytes(rom.data), rom.snes_to_pc(SURF_BLOB))
    blob = bytes(blob) + bytes(0x10000)          # the game reads past the end into a cleared bank
    off = rom.u16(rom.snes_to_pc(TBL_SURF_OFF) + theme * 2)
    t = blob[off:off + 192] + OBJ_SURF
    _cache[key] = t
    return t


def is_solid(c: int) -> bool:   return (c & 0x80) != 0
def is_hazard(c: int) -> bool:  return 0x20 <= c < 0x40
# ROAD reaches $50 (Donut Plains' bridge, terminal speed 849/1000 - NOTES
# 066): the racing line may run on it.  $52 and up (661 and under) is OFF.
def is_road(c: int) -> bool:    return 0x40 <= c <= 0x50 or c in (0x10, 0x12, 0x14, 0x16, 0x18, 0x1A, 0x1C, 0x1E)
def is_off(c: int) -> bool:     return 0x52 <= c < 0x80
def is_driveable(c: int) -> bool: return is_road(c) or is_off(c)


def kind(c: int) -> str:
    """One word per class, for reports and the role compiler."""
    if is_solid(c):
        return "BLOCK" if c >= 0x82 else "WALL"
    if c == 0x22:
        return "WATER"
    if is_hazard(c):
        return "HAZARD"
    if is_road(c):
        return "ROAD"
    if is_off(c):
        return "OFF"
    return "NONE"
