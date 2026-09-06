"""Per-theme tile catalogues, learned from the ROM's own courses, and the
role-map compiler that uses them (docs/TRACKS.md section 6.2).

Everything a catalogue holds is READ off the ROM: the class of every
tile (the surface table), which tiles the theme's courses actually use
and how often, which tile sits next to which (four-neighbour counts over
the theme's own tilemaps), the start-line tile (the one tile that only
ever appears inside the finish rectangles), and which stamp kinds yield
boxes, coins, pads, ramps and oil (the stamps rendered and classified).

What is OURS is the choice rule: interior cells take the role's most
frequent tile, edges the tile the neighbours were most often seen next
to.  Expect odd corners; the invariant the lint holds is that every
cell's CLASS is what the author drew.
"""
from __future__ import annotations
import collections
from .rom import Rom
from . import course as C, mode7 as M, surface as S

TBL_STAMP = 0x84F23D
TBL_SIZES = 0x84F384

# the roles an author paints, and the class family each must compile to
ROLES = {
    "ROAD":   S.is_road,
    "OFF":    S.is_off,
    "WALL":   lambda c: S.is_solid(c) and c < 0x82,
    "BLOCK":  lambda c: c >= 0x82,
    "WATER":  lambda c: c == 0x22,
    "HAZARD": lambda c: S.is_hazard(c) and c != 0x22,
}


def tracks_of_theme(rom: Rom, theme: int):
    return [t for t in range(20) if S.theme_of(rom, t) == theme]


class Catalogue:
    def __init__(self, rom: Rom, theme: int):
        self.rom = rom
        self.theme = theme
        self.cls = S.table(rom, theme)
        self.freq = collections.Counter()          # tile -> cells over the theme's courses
        self.right = collections.Counter()         # (a, b): b is right of a
        self.down = collections.Counter()          # (a, b): b is below a
        self.line_tiles: list[int] = []            # the start line's row, in order
        self.wp_class = collections.Counter()      # class under the ROM's waypoints
        inside = collections.Counter()
        line_rows = []
        for t in tracks_of_theme(rom, theme):
            tm = M.tilemap(rom, t)
            for y in range(128):
                for x in range(128):
                    a = tm[y * 128 + x]
                    self.freq[a] += 1
                    if x < 127:
                        self.right[(a, tm[y * 128 + x + 1])] += 1
                    if y < 127:
                        self.down[(a, tm[(y + 1) * 128 + x])] += 1
            m, n = C.build_sector_map(rom, t)
            for x, y, _ in C.waypoints(rom, t, n)[:n]:
                self.wp_class[self.cls[tm[(y // 8) * 128 + x // 8]]] += 1
            fin = C.mark_finish(rom, t, bytearray(m))
            cx, cy, w, h = fin["cell"] % 64, fin["cell"] // 64, fin["w"], fin["h"]
            for y in range(cy * 2, (cy + h) * 2):
                for x in range(cx * 2, (cx + w) * 2):
                    inside[tm[(y & 127) * 128 + (x & 127)]] += 1
            line_rows.append((tm, cx, cy, w, h))
        # the start line: tiles that live (almost) only inside finish rectangles
        cand = {ti for ti in inside if inside[ti] >= 2 and inside[ti] / self.freq[ti] >= 0.9}
        if cand and line_rows:
            tm, cx, cy, w, h = line_rows[0]
            for y in range(cy * 2, (cy + h) * 2):
                row = [tm[(y & 127) * 128 + (x & 127)] for x in range(cx * 2, (cx + w) * 2)]
                seq = [ti for ti in row if ti in cand]
                if len(seq) >= 2:
                    self.line_tiles = seq
                    break
        # the role -> class the theme's courses use most
        self.role_class = {}
        for role, test in ROLES.items():
            counts = collections.Counter({ti: n for ti, n in self.freq.items() if test(self.cls[ti])})
            if counts:
                by_class = collections.Counter()
                for ti, n in counts.items():
                    by_class[self.cls[ti]] += n
                self.role_class[role] = by_class.most_common(1)[0][0]
        # the road proper is what lies under the racing line
        if self.wp_class:
            self.role_class["ROAD"] = self.wp_class.most_common(1)[0][0]
        self.stamps = stamp_catalogue(rom)

    def tiles_of_role(self, role: str) -> list[int]:
        """Candidate tiles: the role's class, most used first; the class
        the theme's courses use for it, or any tile of the family."""
        test = ROLES[role]
        want = self.role_class.get(role)
        # all 256: Mario Circuit's walls are the object band's 240-243,
        # placed in the tilemap itself (NOTES 044/088)
        if want is not None:
            cand = [ti for ti in range(256) if self.cls[ti] == want and self.freq[ti] > 0]
            if cand:
                return sorted(cand, key=lambda ti: -self.freq[ti])
        cand = [ti for ti in range(256) if test(self.cls[ti]) and self.freq[ti] > 0]
        if not cand:
            cand = [ti for ti in range(192) if test(self.cls[ti])]
        return sorted(cand, key=lambda ti: -self.freq[ti])

    def supports(self, role: str) -> bool:
        return bool(self.tiles_of_role(role))


def stamp_catalogue(rom: Rom) -> dict:
    """Every stamp kind the ROM's 64 graphics can make, by what its tiles
    DO: kind byte -> (family, w, h).  Read by rendering each graphic in
    each size class and classifying its tiles through the object band."""
    out = {}
    szs = rom.snes_to_pc(TBL_SIZES)
    ptrs = rom.snes_to_pc(TBL_STAMP)
    fam_of = {0x14: "box", 0x1A: "coins", 0x18: "oil", 0x16: "pad", 0x10: "ramp"}
    for kind in range(256):
        cls2 = (kind >> 5) & 6
        w, h = rom.data[szs + cls2], rom.data[szs + cls2 + 1]
        sp = ptrs + (kind & 0x3F) * 2
        addr = rom.snes_to_pc(0x840000 | rom.u16(sp))
        if addr + w * h > len(rom.data):
            continue
        tiles = [rom.data[addr + i] for i in range(w * h)]
        fams = collections.Counter()
        for ti in tiles:
            if ti == 0xFF:
                continue
            c = S.OBJ_SURF[ti - 192] if ti >= 192 else None
            if c in fam_of:
                fams[fam_of[c]] += 1
        if fams:
            out[kind] = (fams.most_common(1)[0][0], w, h, len(tiles) - tiles.count(0xFF))
    return out


def stamp_for(cat: dict, family: str, used: collections.Counter | None = None) -> int | None:
    """The kind the ROM's own courses use most for a family, else any."""
    kinds = [k for k, v in cat.items() if v[0] == family]
    if not kinds:
        return None
    if used:
        kinds.sort(key=lambda k: -used[k])
    return kinds[0]


def rom_stamp_usage(rom: Rom) -> collections.Counter:
    used = collections.Counter()
    for t in range(20):
        for k, _, _ in C.objects(rom, t):
            used[k] += 1
    return used


def compile_roles(cat: Catalogue, roles: list[str]) -> tuple[bytearray, list[str]]:
    """roles: 16384 role names, row-major.  Returns the tilemap and a list
    of problems (a role the theme cannot express)."""
    problems = []
    cand = {r: cat.tiles_of_role(r) for r in ROLES}
    for r in ROLES:
        if not cand[r] and r in roles:
            problems.append("the theme has no %s tiles" % r)
    tm = bytearray(16384)
    # neighbour compatibility by class family, for the cells not placed yet
    fam_tiles = {r: set(cand[r]) for r in ROLES}

    def score(ti, left, up, right_role, down_role):
        s = cat.freq[ti] * 0.001
        if left is not None:
            s += cat.right[(left, ti)]
        if up is not None:
            s += cat.down[(up, ti)]
        if right_role and right_role in fam_tiles:
            s += 0.25 * sum(cat.right[(ti, b)] for b in cand[right_role][:6])
        if down_role and down_role in fam_tiles:
            s += 0.25 * sum(cat.down[(ti, b)] for b in cand[down_role][:6])
        return s

    for y in range(128):
        for x in range(128):
            i = y * 128 + x
            r = roles[i]
            if r == "LINE":
                r = "ROAD"
            c = cand.get(r) or cand["ROAD"]
            left = tm[i - 1] if x > 0 else None
            up = tm[i - 128] if y > 0 else None
            rr = roles[i + 1] if x < 127 else None
            dr = roles[i + 128] if y < 127 else None
            best = c[0]
            if len(c) > 1:
                best = max(c[:24], key=lambda ti: score(ti, left, up, rr, dr))
            tm[i] = best
    # the start line: its own tiles across the LINE cells, in the ROM's order
    if cat.line_tiles:
        for y in range(128):
            k = 0
            for x in range(128):
                i = y * 128 + x
                if roles[i] == "LINE":
                    tm[i] = cat.line_tiles[k % len(cat.line_tiles)]
                    k += 1
                else:
                    k = 0
    return tm, problems
