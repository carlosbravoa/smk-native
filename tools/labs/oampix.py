#!/usr/bin/env python3
"""Draw what OAM says the game is drawing, from a VRAM/CGRAM/OAM dump.

    tools/labs/oampix.py flag_0        (reads tmp/flag_{vram,cgram,oam}_0.bin)

S28's lesson made general: OAM names the tiles, sizes and palettes, so a
dump can be turned back into a picture instead of being reasoned about.
Every distinct sprite is rendered once with its tile number, which is what
identifies art that no sheet-reading will find.
"""
import sys, os
from PIL import Image, ImageDraw

OBJ_BASE = 0x4000          # word address (established from the coin tiles)
TMP = "tmp"

def load(tag):
    def rd(k):
        return open(os.path.join(TMP, "%s_%s.bin" % (tag.split('_')[0] + '_' + k,
                                                     tag.split('_')[1])), 'rb').read()
    base, idx = tag.rsplit('_', 1)
    v = open(os.path.join(TMP, "%s_vram_%s.bin" % (base, idx)), 'rb').read()
    c = open(os.path.join(TMP, "%s_cgram_%s.bin" % (base, idx)), 'rb').read()
    o = open(os.path.join(TMP, "%s_oam_%s.bin" % (base, idx)), 'rb').read()
    return v, c, o

def colour(cg, i):
    v = cg[i*2] | cg[i*2+1] << 8
    return ((v & 31)*255//31, ((v >> 5) & 31)*255//31, ((v >> 10) & 31)*255//31)

def tile(v, n):
    off = (OBJ_BASE + n*16) * 2
    out = [[0]*8 for _ in range(8)]
    if off + 32 > len(v): return out
    for y in range(8):
        p0, p1 = v[off+y*2], v[off+y*2+1]
        p2, p3 = v[off+16+y*2], v[off+16+y*2+1]
        for x in range(8):
            b = 7-x
            out[y][x] = ((p0>>b)&1) | (((p1>>b)&1)<<1) | (((p2>>b)&1)<<2) | (((p3>>b)&1)<<3)
    return out

def sprite(v, cg, t, pal, big, hf, vf):
    n = 2 if big else 1
    im = Image.new('RGB', (n*8, n*8), (28, 32, 44))
    for ty in range(n):
        for tx in range(n):
            px = tile(v, t + ty*16 + tx)
            for y in range(8):
                for x in range(8):
                    q = px[y][x]
                    if not q: continue
                    dx, dy = tx*8+x, ty*8+y
                    if hf: dx = n*8-1-dx
                    if vf: dy = n*8-1-dy
                    im.putpixel((dx, dy), colour(cg, 128 + pal*16 + q))
    return im

def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else 'flag_0'
    v, cg, o = load(tag)
    seen, out = {}, []
    for k in range(128):
        y = o[k*4+1]
        if y in (0, 0xF0, 0xE0): continue
        x, t, a = o[k*4], o[k*4+2], o[k*4+3]
        hi = o[512 + (k >> 2)]
        big = (hi >> ((k & 3)*2 + 1)) & 1
        x |= ((hi >> ((k & 3)*2)) & 1) << 8
        t |= (a & 1) << 8
        key = (t, a & 0xFE, big)
        if key in seen: continue
        seen[key] = 1
        out.append((k, x, y, t, a, big,
                    sprite(v, cg, t, (a >> 1) & 7, big, bool(a & 0x40), bool(a & 0x80))))
    print("%d distinct sprites in %s" % (len(out), tag))
    C, W = 10, 64
    rows = (len(out) + C - 1) // C
    img = Image.new('RGB', (C*(W+6), rows*(W+18)), (18, 22, 30))
    d = ImageDraw.Draw(img)
    for i, (k, x, y, t, a, big, im) in enumerate(out):
        cx, cy = (i % C)*(W+6), (i // C)*(W+18)
        img.paste(im.resize((W, W), Image.NEAREST), (cx, cy+16))
        d.text((cx+2, cy+3), "$%03X" % t, fill=(255, 240, 120))
        print("   slot %3d (%3d,%3d) tile $%03X attr $%02X pal %d %s"
              % (k, x, y, t, a, (a >> 1) & 7, "16x16" if big else "8x8"))
    img.save('tmp/%s_sprites.png' % tag)
    print("wrote tmp/%s_sprites.png" % tag)

main()
