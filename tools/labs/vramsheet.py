#!/usr/bin/env python3
"""Every OBJ tile resident during a race, as one sheet: 4bpp 8x8 tiles from
the whole of VRAM, 32 tiles a row, drawn with a given CGRAM sprite
palette (default: each tile with palette PAL) - so the road art for the
items can be found by eye instead of by byte search."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from lab import Lab, log
from smktool.gfx import write_png
PAL = int(os.environ.get("PAL", "8"))
L = Lab(settle=90, zero=(0x0E50, 0x0E51))
v = L.b.vram; cg = L.b.cgram
def rgb(i):
    c = cg[i*2] | cg[i*2+1] << 8
    return ((c & 31) * 8, ((c >> 5) & 31) * 8, ((c >> 10) & 31) * 8)
pal = [rgb(PAL*16 + i) for i in range(16)]
W = 32; NT = len(v) // 32
H = NT // W
img = bytearray(W*8*H*8*3)
for t in range(NT):
    base = t*32
    for y in range(8):
        lo0, lo1 = v[base+y*2], v[base+y*2+1]
        hi0, hi1 = v[base+16+y*2], v[base+16+y*2+1]
        for x in range(8):
            b = 7-x
            c = (lo0>>b&1) | (lo1>>b&1)<<1 | (hi0>>b&1)<<2 | (hi1>>b&1)<<3
            px = ((t//W)*8+y)*(W*8) + (t%W)*8+x
            r,g,bb = pal[c] if c else (24,24,40)
            img[px*3:px*3+3] = bytes((r,g,bb))
out = os.path.join("tmp", "vramsheet_p%d.png" % PAL)
write_png(out, W*8, H*8, bytes(img), scale=2)
log("wrote %s: %d tiles, %d rows; OBSEL $2101 name base bits: %02X" % (out, NT, H, L.b.regs.get(0x2101, 0) if hasattr(L.b, 'regs') else 0))
