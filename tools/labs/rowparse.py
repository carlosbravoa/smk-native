"""Shared reader for rowlog.csv - the header is rebuilt, not trusted.

An early capture shipped a header missing $C1 while the data carried it,
so every column after $96 was misread by one.  The field order is fixed
by rowlog.lua, so building the names here and checking the width is both
safer and self-documenting.
"""
FIELDS = ["C8","DA","E2","E6","10","84","90","92","94","96","C1","spd","x","y"]
TABLE  = [f"t{0x010C + i*2:03X}" for i in range(10)]

def header():
    return ["f"] + TABLE + [f"k{k}{f}" for k in range(8) for f in FIELDS]

def load(path):
    hdr = header(); rows = []
    for l in open(path):
        l = l.strip()
        if not l[:1].isdigit(): continue
        p = l.split(',')
        if len(p) == len(hdr): rows.append([int(v) for v in p])
    return hdr, rows
