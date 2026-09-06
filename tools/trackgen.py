#!/usr/bin/env python3
"""The track creator (docs/TRACKS.md).

  trackgen.py new DIR --theme N [--name NAME]   a package to start from: an
                                                oval drawn in roles.txt
  trackgen.py build DIR          roles.txt -> map.bin + stamps + entities,
                                 then the course data (sectors, line,
                                 finish, grid, segments), then the lint
  trackgen.py gen DIR            the course data from an existing map.bin
  trackgen.py lint DIR           check a package against what the game needs
  trackgen.py render DIR [PNG]   a picture: the track, the sectors, the line
  trackgen.py export N DIR       a ROM course as a package, as is
  trackgen.py from-rom N DIR     a ROM course's map with GENERATED course
                                 data - the generator's benchmark input

roles.txt is 128 lines of 128 characters (lines starting with ';' are comments):
    =  road          .  off-road (grass, dirt, sand, ice - the theme's)
    #  wall          B  breakable block (Ghost Valley, Vanilla Lake)
    ~  shallow water (wade)      space  void / lava / deep water
    S  the start line: ONE horizontal run on the road; karts drive UP from it
    b  item box   c  coin   C  coin scatter   o  oil   p  boost pad
    r  ramp (3 wide, for a road running up/down)   R  ramp (3 tall, left/right)
    e  obstacle: the theme's creature (pipe, mole, plant, fish, Thwomp)
Markers sit on road.  Every ROM byte the game needs stays in the ROM: the
package holds the author's arrangement of the theme's tiles and nothing
else, and the tool never writes ROM data into it.
"""
from __future__ import annotations
import argparse, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from smktool.rom import Rom
from smktool import course as C, mode7 as M, surface as S, tilecat as T, coursegen as G, pkg as P

from smktool.project import (ROLE_OF, MARKERS, parse_roles, template_roles, read_manifest_keys,
                             stamped, build_map, gen_course, make_package, png_write, load_rom)


def read_roles(path: str):
    try:
        return parse_roles(open(path).read())
    except ValueError as e:
        raise SystemExit("%s: %s" % (path, e))


def cmd_new(a):
    os.makedirs(a.dir, exist_ok=True)
    name = a.name or os.path.basename(os.path.normpath(a.dir)).upper()
    with open(os.path.join(a.dir, "roles.txt"), "w") as f:
        f.write("; roles: = road  . off-road  # wall  B block  ~ water  space void   (';' lines are comments)\n")
        f.write("; S start line (karts drive UP from it)  b box  c coin  C coins  o oil  p pad  r/R ramp  e obstacle\n")
        for row in template_roles():
            f.write(row + "\n")
    with open(os.path.join(a.dir, "course.txt"), "w") as f:
        f.write("# smk-port course package (docs/TRACKS.md) - run trackgen.py build on this directory\n")
        f.write("format   1\nname     %s\ntheme    %d\nitems    1\n" % (name, a.theme))
    print("wrote %s/roles.txt and course.txt (theme %d); draw, then: trackgen.py build %s" % (a.dir, a.theme, a.dir))


# ---- build / gen -------------------------------------------------------------

def write_course(d: str, keys: dict, tm: bytes, stamps: list, ents: list, crs: G.Course):
    p = make_package(keys, tm, stamps, ents, crs)
    P.write(p, d)
    return p


def report(crs: G.Course, problems: list, d: str):
    print("  %d sectors, lap %.0f px, finish %s, grid %s, segments %s" % (
        len(crs.line), crs.lap_px, crs.finish, crs.grid, crs.segments))
    for n in crs.notes:
        print("  note:", n)
    if problems:
        print("  LINT: %d problem(s)" % len(problems))
        for q in problems:
            print("   -", q)
    else:
        print("  lint: clean")


def cmd_build(a):
    rom = load_rom()
    keys = read_manifest_keys(a.dir)
    if keys["theme"] is None:
        sys.exit("%s/course.txt needs a theme" % a.dir)
    cat = T.Catalogue(rom, keys["theme"])
    roles, markers = read_roles(os.path.join(a.dir, "roles.txt"))
    line_cells = [i for i, r in enumerate(roles) if r == "LINE"]
    tm, stamps, ents, bprob = build_map(rom, cat, roles, markers)
    for q in bprob:
        print("  !", q)
    try:
        crs, full = gen_course(rom, cat, tm, stamps, ents, line_cells)
    except G.GenError as e:
        sys.exit("cannot generate the course: %s" % e)
    p = write_course(a.dir, keys, tm, stamps, ents, crs)
    problems = G.lint(full, cat.cls, p.sect, p.line, p.finish, p.grid, p.ents)
    # the class of every cell is what was drawn
    cm = [cat.cls[t] for t in tm]
    wrong = 0
    for i, r in enumerate(roles):
        rr = "ROAD" if r == "LINE" else r
        if rr in T.ROLES and not T.ROLES[rr](cm[i]):
            wrong += 1
    if wrong:
        problems.append("%d cells compiled to a class outside their role" % wrong)
    print("built %s: %d stamps, %d entities" % (a.dir, len(stamps), len(ents)))
    report(crs, problems, a.dir)
    return 1 if problems else 0


def cmd_gen(a):
    rom = load_rom()
    p = P.read(a.dir)
    cat = T.Catalogue(rom, p.theme)
    keys = read_manifest_keys(a.dir)
    try:
        crs, full = gen_course(rom, cat, p.map, p.stamps, p.ents)
    except G.GenError as e:
        sys.exit("cannot generate the course: %s" % e)
    q = write_course(a.dir, keys, p.map, p.stamps, p.ents, crs)
    problems = G.lint(full, cat.cls, q.sect, q.line, q.finish, q.grid, q.ents)
    print("generated %s" % a.dir)
    report(crs, problems, a.dir)
    return 1 if problems else 0


def cmd_lint(a):
    rom = load_rom()
    p = P.read(a.dir)
    cat = T.Catalogue(rom, p.theme)
    full = stamped(rom, p.map, p.stamps)
    problems = G.lint(full, cat.cls, p.sect, p.line, p.finish, p.grid, p.ents)
    if problems:
        print("%s: %d problem(s)" % (a.dir, len(problems)))
        for q in problems:
            print("  -", q)
        return 1
    print("%s: clean (%d sectors)" % (a.dir, p.sectors))
    return 0


def cmd_export(a):
    rom = load_rom()
    p = P.from_rom(rom, a.track)
    p.name = a.name or C_name(rom, a.track)
    P.write(p, a.dir)
    print("exported track %d as %s (%d sectors)" % (a.track, a.dir, p.sectors))


def C_name(rom, t):
    return "ROM %02d" % t


def cmd_from_rom(a):
    rom = load_rom()
    p = P.from_rom(rom, a.track)
    cat = T.Catalogue(rom, p.theme)
    keys = {"name": a.name or ("GEN %02d" % a.track), "theme": p.theme, "items": p.items, "music": ""}
    try:
        crs, full = gen_course(rom, cat, p.map, p.stamps, p.ents)
    except G.GenError as e:
        sys.exit("track %d: cannot generate the course: %s" % (a.track, e))
    q = write_course(a.dir, keys, p.map, p.stamps, p.ents, crs)
    problems = G.lint(full, cat.cls, q.sect, q.line, q.finish, q.grid, q.ents)
    print("track %d regenerated as %s (ROM: %d sectors)" % (a.track, a.dir, p.sectors))
    report(crs, problems, a.dir)
    return 1 if problems else 0


# ---- render ----------------------------------------------------------------

def cmd_render(a):
    rom = load_rom()
    p = P.read(a.dir)
    full = stamped(rom, p.map, p.stamps)
    from smktool.compress import decompress
    # the theme's stream may read past its own output (theme 6): the game's
    # WRAM model, so not strict
    packed, _ = decompress(bytes(rom.data), rom.snes_to_pc(M.pointer(rom, M.TILESET_TABLE, p.theme)), strict=False)
    tiles = M.expand_tiles(bytes(packed) + bytes(8192), 192)
    obj, _ = decompress(bytes(rom.data), rom.snes_to_pc(0xC40000))
    tiles += M.expand_tiles(bytes(obj) + bytes(8192), 64)
    pal = M.palette(rom, p.theme)
    cols = [((pal[i * 2] | pal[i * 2 + 1] << 8) & 31) * 8 for i in range(256)]
    cols = [(((pal[i * 2] | pal[i * 2 + 1] << 8)) & 31) * 8 for i in range(256)]
    cols = []
    for i in range(256):
        v = pal[i * 2] | pal[i * 2 + 1] << 8
        cols.append(((v & 31) * 8, ((v >> 5) & 31) * 8, ((v >> 10) & 31) * 8))
    w, h, buf = M.render_track(full, tiles, cols)
    out = bytearray(512 * 512 * 3)
    for y in range(512):
        for x in range(512):
            sx, sy = x * 2, y * 2
            r, g, b = buf[(sy * 1024 + sx) * 3:(sy * 1024 + sx) * 3 + 3]
            c = p.sect[(sy >> 4) * 64 + (sx >> 4)] if p.sect else 0x7F
            if c != 0x7F:
                if c % 2: r = min(255, r + 60)
                else: b = min(255, b + 60)
            out[(y * 512 + x) * 3:(y * 512 + x) * 3 + 3] = bytes((r, g, b))
    fx, fy, fw, fh = p.finish
    for cy in range(fy, fy + fh):
        for cx in range(fx, fx + fw):
            for k in range(8):
                for (x, y) in ((cx * 8 + k, cy * 8), (cx * 8 + k, cy * 8 + 7), (cx * 8, cy * 8 + k), (cx * 8 + 7, cy * 8 + k)):
                    if 0 <= x < 512 and 0 <= y < 512:
                        i = (y * 512 + x) * 3
                        out[i] = 255; out[i + 1] = 255; out[i + 2] = 255
    for i, (px, py, attr) in enumerate(p.line):
        colr = (255, 255, 255) if i == 0 else (255, 0, 255) if attr != 3 else (0, 255, 0)
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                x, y = (px // 2 + dx) & 511, (py // 2 + dy) & 511
                out[(y * 512 + x) * 3:(y * 512 + x) * 3 + 3] = bytes(colr)
    gx, gy, gs = p.grid
    for slot in range(8):
        x, y = (gx + (gs if slot & 1 else 0)) // 2, (gy + 24 * slot) // 2
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                if 0 <= x + dx < 512 and 0 <= y + dy < 512:
                    out[((y + dy) * 512 + x + dx) * 3:((y + dy) * 512 + x + dx) * 3 + 3] = bytes((255, 255, 0))
    path = a.png or os.path.join(a.dir, "preview.png")
    png_write(path, 512, 512, out)
    print("wrote", path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("new"); s.add_argument("dir"); s.add_argument("--theme", type=int, required=True); s.add_argument("--name"); s.set_defaults(fn=cmd_new)
    s = sub.add_parser("build"); s.add_argument("dir"); s.set_defaults(fn=cmd_build)
    s = sub.add_parser("gen"); s.add_argument("dir"); s.set_defaults(fn=cmd_gen)
    s = sub.add_parser("lint"); s.add_argument("dir"); s.set_defaults(fn=cmd_lint)
    s = sub.add_parser("render"); s.add_argument("dir"); s.add_argument("png", nargs="?"); s.set_defaults(fn=cmd_render)
    s = sub.add_parser("export"); s.add_argument("track", type=int); s.add_argument("dir"); s.add_argument("--name"); s.set_defaults(fn=cmd_export)
    s = sub.add_parser("from-rom"); s.add_argument("track", type=int); s.add_argument("dir"); s.add_argument("--name"); s.set_defaults(fn=cmd_from_rom)
    a = ap.parse_args()
    sys.exit(a.fn(a) or 0)


if __name__ == "__main__":
    main()
