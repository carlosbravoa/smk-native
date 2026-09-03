#!/usr/bin/env python3
"""Render VRAM 4bpp tiles rows R0..R1 (32 tiles a row) with sprite palette
P (0..7) at 4x: tools/labs/vramrender.py R0 R1 P [out.png]"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from smktool.gfx import write_png
r0, r1, P = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
C0 = int(os.environ.get("C0", "0")); C1 = int(os.environ.get("C1", "32")); SC = int(os.environ.get("SC", "4"))
out = sys.argv[4] if len(sys.argv) > 4 else "tmp/vram_r%d_%d_c%d_%d_p%d.png" % (r0, r1, C0, C1, P)
v = open("assets/captures/vram.bin","rb").read(); cg = open("assets/captures/cgram.bin","rb").read()
def rgb(i):
    c = cg[i*2] | cg[i*2+1] << 8
    return ((c & 31) * 8, ((c >> 5) & 31) * 8, ((c >> 10) & 31) * 8)
pal = [rgb(128 + P*16 + i) for i in range(16)]
W = C1 - C0; H = r1 - r0
img = bytearray(W*8*H*8*3)
for row in range(r0, r1):
    for col in range(C0, C1):
        t = row*32 + col; base = t*32
        for y in range(8):
            lo0, lo1, hi0, hi1 = v[base+y*2], v[base+y*2+1], v[base+16+y*2], v[base+16+y*2+1]
            for x in range(8):
                b = 7-x
                c = (lo0>>b&1) | (lo1>>b&1)<<1 | (hi0>>b&1)<<2 | (hi1>>b&1)<<3
                px = ((row-r0)*8+y)*(W*8) + (col-C0)*8+x
                img[px*3:px*3+3] = bytes(pal[c] if c else (24,24,40))
write_png(out, W*8, H*8, bytes(img), scale=SC)
print("wrote", out)
