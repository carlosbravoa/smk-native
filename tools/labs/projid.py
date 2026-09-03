#!/usr/bin/env python3
"""Every projectile-row sprite the recordings ever drew, rendered with its
own palette and named by its ROM source.

    tools/labs/projid.py tmp/pc_<rec> ...      (needs tmp/log_<rec>.txt)

Output: tmp/projid.png, a labelled sheet, and one line per (char, palette).
"""
import sys, os, re
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, ".."))
from smktool.rom import Rom
from spritefind import Locator
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(HERE))
rom = Rom.load(os.path.join(ROOT, "rom", "smk_usa.sfc"))
loc = Locator(rom)
cells = []
for pre in sys.argv[1:]:
    rec = os.path.basename(pre)[3:]
    logp = os.path.join(os.path.dirname(pre), "log_%s.txt" % rec)
    if not (os.path.exists(pre + ".vram") and os.path.exists(logp)):
        print("skip", rec); continue
    vram = open(pre + ".vram", "rb").read(); cg = open(pre + ".cgram", "rb").read()
    def col(i):
        v = cg[i * 2] | (cg[i * 2 + 1] << 8)
        return ((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31)
    for l in open(logp):
        m = re.match(r"proj \$([0-9A-F]{2}) p(\d) first f(\d+) at \((\d+),(\d+)\)", l)
        if not m: continue
        ch, pal, fr, x, y = int(m.group(1), 16), int(m.group(2)), int(m.group(3)), int(m.group(4)), int(m.group(5))
        t = 0x400 + ch
        raw = bytes(vram[t * 32:t * 32 + 32])
        im = Image.new("RGB", (8, 8), (40, 40, 52))
        for yy in range(8):
            b0, b1, b2, b3 = raw[yy * 2], raw[yy * 2 + 1], raw[16 + yy * 2], raw[16 + yy * 2 + 1]
            for xx in range(8):
                s = 7 - xx
                v = ((b0 >> s) & 1) | (((b1 >> s) & 1) << 1) | (((b2 >> s) & 1) << 2) | (((b3 >> s) & 1) << 3)
                if v: im.putpixel((xx, yy), col(128 + pal * 16 + v))
        where = loc.where(raw)
        print("%-12s char $%02X pal %d  first f%-6d at (%3d,%3d)  VRAM $%03X <- %s" % (rec, ch, pal, fr, x, y, t, where))
        cells.append(("%s $%02X p%d" % (rec[:6], ch, pal), im))
if cells:
    cols = 8; rows = (len(cells) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * 64, rows * 22), (22, 22, 30)); dr = ImageDraw.Draw(sheet)
    for k, (lab, im) in enumerate(cells):
        sheet.paste(im.resize((16, 16), Image.NEAREST), ((k % cols) * 64 + 2, (k // cols) * 22 + 2))
        dr.text(((k % cols) * 64 + 20, (k // cols) * 22 + 4), lab, fill=(200, 200, 210))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(os.path.join(ROOT, "tmp", "projid.png"))
    print("wrote tmp/projid.png (%d sprites)" % len(cells))
