"""Composite the oracle's PPU state into a picture.

Renders the sprite layer from OAM + VRAM + CGRAM exactly as configured
(OBSEL base, sizes, flips, palettes, priority by OAM index).  Backgrounds
are not rendered - the Mode 7 layer needs per-scanline HDMA state we do not
model - so this is an inspection tool, not an emulator display.
"""
from __future__ import annotations
from .gfx import decode_tile, write_png, read_palette


def sprite_entries(oam: bytes):
    """Decoded OAM: (index, x, y, tile9, attr, big) with sign-extended X."""
    out = []
    for i in range(128):
        x, y, t, a = oam[i * 4:i * 4 + 4]
        hi = oam[512 + (i >> 2)]
        xh = (hi >> ((i & 3) * 2)) & 1
        big = (hi >> ((i & 3) * 2 + 1)) & 1
        out.append((i, x - (256 if xh else 0), y, t | ((a & 1) << 8), a, big))
    return out


def render_sprites(vram: bytes, cgram: bytes, oam: bytes, obsel: int,
                   bg=(40, 44, 52)) -> tuple[int, int, bytes]:
    base = (obsel & 7) << 14
    pal = read_palette(cgram, 0, 256)
    W, H = 256, 224
    buf = bytearray(bytes(bg) * (W * H))
    # lowest OAM index wins, so draw back-to-front
    for i, x, y, t9, a, big in reversed(sprite_entries(oam)):
        if y == 0xF0:
            continue                      # the conventional off-screen park
        n = 2 if big else 1               # sizes for OBSEL size-select 0
        palb = 0x80 + ((a >> 1) & 7) * 16
        hf, vf = (a >> 6) & 1, (a >> 7) & 1
        for tr in range(n):
            for tc in range(n):
                src_tr = (n - 1 - tr) if vf else tr
                src_tc = (n - 1 - tc) if hf else tc
                off = (base + ((t9 + src_tr * 16 + src_tc) & 0x1FF) * 32) & 0xFFFF
                tile = decode_tile(vram, off, 4)
                for yy in range(8):
                    for xx in range(8):
                        c = tile[(7 - yy) if vf else yy][(7 - xx) if hf else xx]
                        if c == 0:
                            continue
                        dx, dy = x + tc * 8 + xx, y + tr * 8 + yy
                        if 0 <= dx < W and 0 <= dy < H:
                            p = (dy * W + dx) * 3
                            buf[p:p + 3] = bytes(pal[palb + c])
    return W, H, bytes(buf)
