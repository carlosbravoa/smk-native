#!/usr/bin/env python3
"""Rebuild the PLAYER'S KART from VRAM, neutral vs steering, and look at it.

    tools/labs/leanpix.py            (after headlean.py has dumped)

The user: "the images you are providing are not leaning but turning.
different things."  Our port picks a rotation FRAME when you steer at a
standstill, so the whole kart pivots.  The game changes only about 70
bytes - roughly two tiles - where a whole rotation frame is 512, so it is
moving the driver and not the kart.

Counting bytes is how that was misread in the first place, so this draws
the sprite instead: find the player's kart in OAM, decode its tiles out
of VRAM with the game's own palette, and put neutral, left and right side
by side.
"""
import sys, os
from PIL import Image, ImageDraw

OBJ_BASE = 0x4000          # word address, established from the coin tiles

def load(name):
    v = open('tmp/lean_%s_vram.bin' % name, 'rb').read()
    o = open('tmp/lean_%s_oam.bin' % name, 'rb').read()
    c = open('tmp/lean_%s_cgram.bin' % name, 'rb').read()
    return v, o, c

def colour(cg, i):
    v = cg[i*2] | cg[i*2+1] << 8
    return ((v & 31)*255//31, ((v >> 5) & 31)*255//31, ((v >> 10) & 31)*255//31)

def tile(v, n):
    off = (OBJ_BASE + n*16) * 2
    out = [[0]*8 for _ in range(8)]
    for y in range(8):
        p0, p1 = v[off+y*2], v[off+y*2+1]
        p2, p3 = v[off+16+y*2], v[off+16+y*2+1]
        for x in range(8):
            b = 7-x
            out[y][x] = ((p0>>b)&1) | (((p1>>b)&1)<<1) | (((p2>>b)&1)<<2) | (((p3>>b)&1)<<3)
    return out

def sprites(o):
    """visible OAM entries as (slot, x, y, tile, attr, big)"""
    out = []
    for k in range(128):
        x, y, t, a = o[k*4], o[k*4+1], o[k*4+2], o[k*4+3]
        hi = o[512 + (k >> 2)]
        big = (hi >> ((k & 3)*2 + 1)) & 1
        x |= ((hi >> ((k & 3)*2)) & 1) << 8
        if y in (0, 0xF0, 0xE0): continue
        out.append((k, x, y, t | ((a & 1) << 8), a, big))
    return out

def draw(v, cg, base_tile, pal, w_tiles, h_tiles):
    im = Image.new('RGB', (w_tiles*8, h_tiles*8), (28, 32, 44))
    for ty in range(h_tiles):
        for tx in range(w_tiles):
            px = tile(v, base_tile + ty*16 + tx)
            for y in range(8):
                for x in range(8):
                    q = px[y][x]
                    if q: im.putpixel((tx*8+x, ty*8+y), colour(cg, 128 + pal*16 + q))
    return im

# The player's kart is FOUR 16x16 sprites in a 2x2, and neutral is
# symmetric: the right pair is the left pair with hflip set.  Steering
# swaps only the RIGHT pair to its own tiles ($180 -> $182), so the sprite
# stops mirroring itself - the driver leans, the kart does not turn.
def kart_of(o):
    out = []
    for k in (28, 29, 30, 31):
        x, y, t, a = o[k*4], o[k*4+1], o[k*4+2], o[k*4+3]
        out.append((x, y, t | ((a & 1) << 8), a))
    return out

def draw16(v, cg, base, pal, hflip):
    im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
    for ty in range(2):
        for tx in range(2):
            px = tile(v, base + ty*16 + tx)
            for y in range(8):
                for x in range(8):
                    q = px[y][x]
                    if not q: continue
                    dx = tx*8 + x
                    if hflip: dx = 15 - dx
                    im.putpixel((dx, ty*8 + y), colour(cg, 128 + pal*16 + q) + (255,))
    return im

panels = []
for name in ('neutral', 'left', 'right'):
    v, o, cg = load(name)
    im = Image.new('RGB', (32, 32), (28, 32, 44))
    tiles = []
    for (x, y, t, a) in kart_of(o):
        pal = (a >> 1) & 7
        hf = bool(a & 0x40)
        part = draw16(v, cg, t, pal, hf)
        ox = 0 if x < 120 else 16
        oy = 0 if y < 78 else 16
        im.paste(part, (ox, oy), part)
        tiles.append("$%03X%s" % (t, "F" if hf else " "))
    print("%-8s tiles %s" % (name, " ".join(tiles)))
    panels.append((name, im))

W = 160
out = Image.new('RGB', (len(panels)*(W+8), W+18), (18, 22, 30))
d = ImageDraw.Draw(out)
for i, (n, im) in enumerate(panels):
    out.paste(im.resize((W, W), Image.NEAREST), (i*(W+8), 18))
    d.text((i*(W+8)+4, 4), n, fill=(255, 240, 120))
out.save('tmp/gamelean.png')
print("wrote tmp/gamelean.png")
