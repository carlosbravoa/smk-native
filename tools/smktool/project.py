"""A course project: the author's roles and markers, and everything the
tools derive from them (docs/TRACKS.md section 6.2).

Shared by tools/trackgen.py (the command line) and tools/trackstudio.py
(the editor).  A project's source of truth is roles.txt plus the few
manifest keys; build() compiles the tiles, generates the course data,
lints, and writes the package the game loads.  Nothing here opens a
window and nothing here needs more than the standard library.
"""
from __future__ import annotations
import os, struct, subprocess, zlib
from .rom import Rom
from . import course as C, mode7 as M, surface as S, tilecat as T, coursegen as G, pkg as P
from .compress import decompress

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROM_PATH = os.environ.get("SMK_ROM", os.path.join(ROOT, "rom", "smk_usa.sfc"))

ROLE_OF = {"=": "ROAD", ".": "OFF", "#": "WALL", "B": "BLOCK", "~": "WATER", " ": "HAZARD",
           "S": "LINE"}
CHAR_OF = {v: k for k, v in ROLE_OF.items()}
MARKERS = {"b": "box", "c": "coin", "C": "coins", "o": "oil", "p": "pad", "r": "ramp", "R": "rampv", "e": "entity"}
MARKER_CHAR = {v: k for k, v in MARKERS.items()}
# a marker's footprint in tiles (w, h), from the ROM's stamp sizes
FOOTPRINT = {"box": (2, 2), "coin": (3, 1), "coins": (5, 5), "oil": (2, 2), "pad": (2, 2),
             "ramp": (3, 1), "rampv": (1, 3), "entity": (1, 1)}
THEME_NAMES = ["GHOST VALLEY", "MARIO CIRCUIT", "DONUT PLAINS", "CHOCO ISLAND",
               "VANILLA LAKE", "KOOPA BEACH", "BOWSER CASTLE", "RAINBOW ROAD"]
# flat colours for the roles view, one per role
ROLE_RGB = {"ROAD": (120, 116, 140), "OFF": (150, 120, 70), "WALL": (60, 60, 60), "BLOCK": (200, 200, 220),
            "WATER": (70, 120, 200), "HAZARD": (20, 20, 30), "LINE": (240, 240, 240)}
MARKER_RGB = {"box": (255, 210, 40), "coin": (255, 240, 120), "coins": (255, 240, 120), "oil": (30, 30, 30),
              "pad": (255, 120, 40), "ramp": (200, 80, 255), "rampv": (200, 80, 255), "entity": (60, 220, 90)}

_rom = None


def load_rom() -> Rom:
    global _rom
    if _rom is None:
        if not os.path.exists(ROM_PATH):
            raise FileNotFoundError("ROM missing: %s (set SMK_ROM)" % ROM_PATH)
        _rom = Rom.load(ROM_PATH)
    return _rom


_cats: dict[int, T.Catalogue] = {}


def catalogue(theme: int) -> T.Catalogue:
    if theme not in _cats:
        _cats[theme] = T.Catalogue(load_rom(), theme)
    return _cats[theme]


# ---- roles.txt ---------------------------------------------------------------

def parse_roles(text: str):
    """roles.txt text -> (roles: list of 16384 names, markers: [(family, x, y)])."""
    rows = [ln.rstrip("\n") for ln in text.splitlines() if not ln.startswith(";")][:128]
    while len(rows) < 128:
        rows.append("")
    roles = ["HAZARD"] * (128 * 128)
    markers = []
    for y in range(128):
        row = rows[y].ljust(128)[:128]
        for x, ch in enumerate(row):
            if ch in ROLE_OF:
                roles[y * 128 + x] = ROLE_OF[ch]
            elif ch in MARKERS:
                roles[y * 128 + x] = "ROAD"
                markers.append((MARKERS[ch], x, y))
            else:
                raise ValueError("roles line %d column %d: unknown character %r" % (y + 1, x + 1, ch))
    return roles, markers


def format_roles(roles, markers) -> str:
    grid = [[CHAR_OF[roles[y * 128 + x]] for x in range(128)] for y in range(128)]
    for fam, x, y in markers:
        if 0 <= x < 128 and 0 <= y < 128:
            grid[y][x] = MARKER_CHAR[fam]
    out = ["; roles: = road  . off-road  # wall  B block  ~ water  space void   (';' lines are comments)",
           "; S start line (karts drive UP from it)  b box  c coin  C coins  o oil  p pad  r/R ramp  e obstacle"]
    out += ["".join(r) for r in grid]
    return "\n".join(out) + "\n"


def template_roles() -> list[str]:
    """An oval on a 128x128 canvas: a two-tile wall round the map, off-road
    inside, a road 12 tiles wide, the line on the left straight."""
    g = [[" "] * 128 for _ in range(128)]
    for y in range(128):
        for x in range(128):
            g[y][x] = "#" if x < 2 or y < 2 or x > 125 or y > 125 else "."
    cx, cy = 64, 64
    for y in range(128):
        for x in range(128):
            dx = max(abs(x - cx) - 16, 0)
            dy = max(abs(y - cy) - 8, 0)
            d = (dx * dx + dy * dy) ** 0.5
            if 22 <= d <= 34:
                g[y][x] = "="
    for x in range(14, 27):
        if g[70][x] == "=":
            g[70][x] = "S"
    for x in range(52, 76, 4):
        g[100][x] = "b"
    for x in range(54, 76, 4):
        g[26][x] = "c"
    g[36][32] = "p"
    g[97][86] = "C"
    for y in range(50, 80, 7):
        g[y][107] = "e"
    return ["".join(r) for r in g]


def blank_roles() -> list[str]:
    g = [["#" if x < 2 or y < 2 or x > 125 or y > 125 else "." for x in range(128)] for y in range(128)]
    return ["".join(r) for r in g]


# ---- the start line, chosen for the author ------------------------------------

GRID_TILES = 27     # rows of ground the eight karts need behind the line (40 px + 7 x 24 px)


def auto_line(roles: list, markers: list = ()) -> list[int] | None:
    """Where the start line goes if nobody drew one: across the longest
    stretch of road running north, at a third of the way up it, so the
    grid (eight rows, 24 px apart, 40 px behind the line) sits on the
    straight and the karts drive up the rest of it.  None if no straight
    is long enough."""
    road = [r in ("ROAD", "LINE") for r in roles]
    # the karts behind the line may stand on grass (the lint allows it, the
    # ROM's grids do it); the road ahead must be road
    ground = [r in ("ROAD", "LINE", "OFF") for r in roles]
    up = [0] * 16384
    for y in range(1, 128):
        for x in range(128):
            i = y * 128 + x
            if road[i] and road[i - 128]:
                up[i] = up[i - 128] + 1
    down = [0] * 16384
    down_road = [0] * 16384
    for y in range(126, -1, -1):
        for x in range(128):
            i = y * 128 + x
            if ground[i] and ground[i + 128]:
                down[i] = down[i + 128] + 1
            if road[i] and road[i + 128]:
                down_road[i] = down_road[i + 128] + 1
    best, score = None, 0
    for y in range(4, 124):
        x = 0
        while x < 128:
            if not road[y * 128 + x]:
                x += 1
                continue
            x0 = x
            while x < 128 and road[y * 128 + x]:
                x += 1
            run = range(x0, x)
            # a road's width, not a straight running left to right
            if len(run) < 5 or len(run) > 24:
                continue
            # the grid stands on the middle of the road: judge the straight
            # by the run's central five tiles, not its kerbs
            mid = (run[0] + run[-1]) // 2
            core = range(max(run[0], mid - 2), min(run[-1], mid + 2) + 1)
            above = min(up[y * 128 + xx] for xx in core)
            below = min(down[y * 128 + xx] for xx in core)
            below_road = min(down_road[y * 128 + xx] for xx in core)
            # road ahead, the front rows of the grid on road, the rest on ground
            if below < GRID_TILES or below_road < 12 or above < 6:
                continue
            length = above + below
            # prefer long straights, and a line a third of the way up them
            want = length // 3
            pen = abs(below - max(GRID_TILES, want))
            # not through the objects: a line or a grid over a box row or a
            # pipe is a poor start
            for fam, mx, my in markers:
                if run[0] - 2 <= mx <= run[-1] + 2 and y - 8 <= my <= y + GRID_TILES:
                    pen += 60
            sc = length * 4 - pen + len(run)
            if sc > score:
                score, best = sc, (y, x0, x - 1)
    if best is None:
        return None
    y, x0, x1 = best
    return [y * 128 + xx for xx in range(x0, x1 + 1)]


def apply_auto_line(roles: list, markers: list = ()) -> bool:
    """Draw the automatic start line into the roles; False if none fits."""
    cells = auto_line(roles, markers)
    if not cells:
        return False
    for i, r in enumerate(roles):
        if r == "LINE":
            roles[i] = "ROAD"
    for i in cells:
        roles[i] = "LINE"
    return True


def start_preview(roles: list, markers: list = (), automatic: bool = True):
    """Where the start is right now: the drawn line, or (if asked) the
    automatic one; returns (line_cells, finish, grid, automatic) or None."""
    cells = [i for i, r in enumerate(roles) if r == "LINE"]
    auto = False
    if not cells and automatic:
        cells = auto_line(roles, markers) or []
        auto = True
    if not cells:
        return None
    finish, grid = G.start_geometry(cells)
    return cells, finish, grid, auto


# ---- the build ---------------------------------------------------------------

def read_manifest_keys(d: str) -> dict:
    keys = {"name": os.path.basename(os.path.normpath(d)).upper(), "theme": None, "items": 1, "music": ""}
    path = os.path.join(d, "course.txt")
    if os.path.exists(path):
        for raw in open(path):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            k, _, v = line.partition(" ")
            v = v.strip()
            if k in ("name", "music"):
                keys[k] = v
            elif k in ("theme", "items"):
                keys[k] = int(v)
    return keys


def stamped(rom: Rom, tm: bytes, stamps: list) -> bytes:
    """The map as the game sees it: the stamps blitted ($84F1A4)."""
    out = bytearray(tm)
    szs = rom.snes_to_pc(T.TBL_SIZES)
    ptrs = rom.snes_to_pc(T.TBL_STAMP)
    for kind, col, row in stamps:
        cls2 = (kind >> 5) & 6
        w, h = rom.data[szs + cls2], rom.data[szs + cls2 + 1]
        addr = rom.snes_to_pc(0x840000 | rom.u16(ptrs + (kind & 0x3F) * 2))
        for r in range(h):
            for c in range(w):
                t = rom.data[addr + r * w + c]
                at = (row + r) * 128 + col + c
                if t != 0xFF and at < 16384 and col + c < 128:
                    out[at] = t
    return bytes(out)


def build_map(rom: Rom, cat: T.Catalogue, roles: list, markers: list):
    """roles + markers -> (tilemap, stamps, entities, problems, notes)."""
    tm, problems, notes = T.compile_roles(cat, roles)
    used = T.rom_stamp_usage(rom)
    kinds = {fam: T.stamp_for(cat.stamps, fam, used) for fam in ("box", "coins", "oil", "pad", "ramp")}
    ramps = [k for k, v in cat.stamps.items() if v[0] == "ramp"]
    ramp_h = sorted([k for k in ramps if cat.stamps[k][1] == 3 and cat.stamps[k][2] == 1], key=lambda k: -used[k])
    ramp_v = sorted([k for k in ramps if cat.stamps[k][1] == 1 and cat.stamps[k][2] == 3], key=lambda k: -used[k])
    coin1 = sorted([k for k, v in cat.stamps.items() if v[0] == "coins" and v[3] == 1], key=lambda k: -used[k])
    stamps, ents = [], []
    for fam, x, y in markers:
        if fam == "entity":
            ents.append((x, y, 0))
            continue
        if fam == "coin":
            k = coin1[0] if coin1 else kinds["coins"]
        elif fam == "ramp":
            k = ramp_h[0] if ramp_h else kinds["ramp"]
        elif fam == "rampv":
            k = ramp_v[0] if ramp_v else kinds["ramp"]
        else:
            k = kinds[fam]
        if k is None:
            problems.append("no stamp for %s" % fam)
            continue
        if len(stamps) >= 42:
            problems.append("more than 42 stamps: %s at %d,%d dropped" % (fam, x, y))
            continue
        stamps.append((k, x, y))
    return tm, stamps, ents, problems, notes


def gen_course(rom: Rom, cat: T.Catalogue, tm: bytes, stamps: list, ents: list, line_cells=None):
    full = stamped(rom, tm, stamps)
    if line_cells is None:
        line_cells = G.find_line_cells(tm, cat.line_tiles)
    return G.generate(full, cat.cls, line_cells, stamps, ents, cat.stamps), full


def make_package(keys: dict, tm: bytes, stamps: list, ents: list, crs: G.Course) -> P.Package:
    p = P.Package()
    p.name = keys["name"]; p.theme = keys["theme"]; p.items = keys["items"]; p.music = keys.get("music", "")
    p.map = tm
    p.stamps = stamps
    order = getattr(crs, "ent_order", list(range(len(ents))))
    p.ents = [ents[k] for k in order]
    p.sect = bytes(crs.sect)
    p.line = crs.line
    p.finish = crs.finish
    p.grid = crs.grid
    p.segments = crs.segments
    return p


def lint_package(p: P.Package) -> list[str]:
    rom = load_rom()
    cat = catalogue(p.theme)
    full = stamped(rom, p.map, p.stamps)
    return G.lint(full, cat.cls, p.sect, p.line, p.finish, p.grid, p.ents)


class Project:
    """What the editor holds: roles, markers, the manifest keys, and the
    last build's package and verdict."""
    def __init__(self):
        self.dir = ""
        self.name = "MY COURSE"
        self.theme = 1
        self.items = 1
        self.music = ""
        self.roles = ["HAZARD"] * 16384
        self.markers = []            # (family, x, y)
        self.package: P.Package | None = None
        self.problems: list[str] = []
        self.notes: list[str] = []
        self.full: bytes | None = None      # the built map with stamps

    # -- files --
    @classmethod
    def new(cls, name: str, theme: int, oval: bool = True, with_line: bool = True) -> "Project":
        pr = cls()
        pr.name, pr.theme = name, theme
        pr.roles, pr.markers = parse_roles("\n".join(template_roles() if oval else blank_roles()))
        if not with_line:
            pr.roles = ["ROAD" if r == "LINE" else r for r in pr.roles]
        return pr

    @classmethod
    def load(cls, d: str) -> "Project":
        pr = cls()
        pr.dir = d
        keys = read_manifest_keys(d)
        pr.name = keys["name"]; pr.theme = keys["theme"] if keys["theme"] is not None else 1
        pr.items = keys["items"]; pr.music = keys["music"]
        rp = os.path.join(d, "roles.txt")
        if os.path.exists(rp):
            pr.roles, pr.markers = parse_roles(open(rp).read())
        else:
            # a package without roles (an export): roles from the classes
            p = P.read(d)
            cat = catalogue(p.theme)
            pr.roles = [S.kind(cat.cls[t]) if S.kind(cat.cls[t]) in ROLE_RGB else "HAZARD" for t in p.map]
            for i, t in enumerate(p.map):
                if t in cat.line_tiles:
                    pr.roles[i] = "LINE"
            pr.markers = []
        try:
            pr.package = P.read(d) if os.path.exists(os.path.join(d, "map.bin")) else None
            if pr.package and pr.package.sect:
                pr.full = stamped(load_rom(), pr.package.map, pr.package.stamps)
        except Exception:
            pr.package = None
        return pr

    def keys(self) -> dict:
        return {"name": self.name, "theme": self.theme, "items": self.items, "music": self.music}

    def save_roles(self, d: str) -> None:
        os.makedirs(d, exist_ok=True)
        with open(os.path.join(d, "roles.txt"), "w") as f:
            f.write(format_roles(self.roles, self.markers))
        cp = os.path.join(d, "course.txt")
        if not os.path.exists(cp):
            with open(cp, "w") as f:
                f.write("# smk-port course package (docs/TRACKS.md)\nformat   1\nname     %s\ntheme    %d\nitems    %d\n"
                        % (self.name, self.theme, self.items))
        self.dir = d

    # -- the build --
    def build(self, d: str, progress=None, automatic_line: bool = True) -> bool:
        """Compile, generate, lint, write the package.  False if the course
        could not be generated (the problems say why).  automatic_line: lay
        a start line if none is drawn (the command line does; the editor
        asks the author for one instead)."""
        def say(msg):
            if progress:
                progress(msg)
        rom = load_rom()
        say("reading the theme's catalogue")
        cat = catalogue(self.theme)
        say("compiling the tiles")
        tm, stamps, ents, problems, notes = build_map(rom, cat, self.roles, self.markers)
        line_cells = [i for i, r in enumerate(self.roles) if r == "LINE"]
        if not line_cells:
            if automatic_line and apply_auto_line(self.roles, self.markers):
                line_cells = [i for i, r in enumerate(self.roles) if r == "LINE"]
                self.auto_lined = True
            else:
                self.problems = [("no start line: pick the Start line tool and click the road where the race "
                                  "starts (the karts drive up from it)") if not automatic_line else
                                 ("no start line, and no straight of road long enough for one: the karts "
                                  "need about 30 tiles of road running north behind the line - draw the "
                                  "straight, or click the line where you want it")] + problems
                self.notes = notes
                self.package = None
                self.full = stamped(rom, tm, stamps)
                self.save_roles(d)
                return False
        say("generating the course")
        try:
            crs, full = gen_course(rom, cat, tm, stamps, ents, line_cells)
        except G.GenError as e:
            self.problems = ["cannot generate the course: %s" % e] + problems
            self.notes = notes
            self.package = None
            self.full = stamped(rom, tm, stamps)
            self.save_roles(d)
            return False
        self.package = make_package(self.keys(), tm, stamps, ents, crs)
        say("writing the package")
        self.save_roles(d)
        P.write(self.package, d)
        say("checking")
        problems += G.lint(full, cat.cls, self.package.sect, self.package.line,
                           self.package.finish, self.package.grid, self.package.ents)
        cm = [cat.cls[t] for t in tm]
        eff = T.effective_roles(cat)
        wrong = 0
        for i, r in enumerate(self.roles):
            rr = "ROAD" if r == "LINE" else eff.get(r, r)
            if rr in T.ROLES and not T.ROLES[rr](cm[i]):
                wrong += 1
        if wrong:
            problems.append("%d cells compiled to a class outside their role" % wrong)
        self.problems = problems
        self.notes = notes + list(crs.notes)
        if getattr(self, "auto_lined", False):
            self.notes.insert(0, "the start line was placed for you, across the longest straight running "
                                 "north; the Start line tool moves it")
            self.auto_lined = False
        self.full = full
        return True

    def save_line(self) -> None:
        """Write back an edited racing line (the editor moves waypoints)."""
        if self.package and self.dir:
            P.write(self.package, self.dir)


# ---- pictures ---------------------------------------------------------------

_tile_rows: dict[int, list] = {}


def theme_tile_rows(theme: int) -> list:
    """Per tile, its eight rows as 24-byte RGB strings, from the user's ROM."""
    if theme in _tile_rows:
        return _tile_rows[theme]
    rom = load_rom()
    packed, _ = decompress(bytes(rom.data), rom.snes_to_pc(M.pointer(rom, M.TILESET_TABLE, theme)), strict=False)
    tiles = M.expand_tiles(bytes(packed) + bytes(8192), 192)
    obj, _ = decompress(bytes(rom.data), rom.snes_to_pc(0xC40000))
    tiles += M.expand_tiles(bytes(obj) + bytes(8192), 64)
    pal = M.palette(rom, theme)
    cols = []
    for i in range(256):
        v = pal[i * 2] | pal[i * 2 + 1] << 8
        cols.append(bytes(((v & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, ((v >> 10) & 31) * 255 // 31)))
    rows = []
    for t in range(256):
        rows.append([b"".join(cols[tiles[t * 64 + py * 8 + px]] for px in range(8)) for py in range(8)])
    _tile_rows[theme] = rows
    return rows


def picture_tiles(theme: int, tm: bytes) -> bytes:
    """1024x1024 RGB of a tilemap as the game draws it."""
    rows = theme_tile_rows(theme)
    out = []
    for y in range(128):
        base = y * 128
        line = [rows[tm[base + x]] for x in range(128)]
        for py in range(8):
            out.append(b"".join(r[py] for r in line))
    return b"".join(out)


def picture_roles(roles: list, markers: list) -> bytes:
    """1024x1024 RGB of the roles as flat colour, markers on top."""
    px = {r: bytes(c) * 8 for r, c in ROLE_RGB.items()}
    grid = [px[roles[i]] for i in range(16384)]
    for fam, x, y in markers:
        w, h = FOOTPRINT[fam]
        for dy in range(h):
            for dx in range(w):
                if x + dx < 128 and y + dy < 128:
                    grid[(y + dy) * 128 + x + dx] = bytes(MARKER_RGB[fam]) * 8
    out = []
    for y in range(128):
        row = b"".join(grid[y * 128:(y + 1) * 128])
        out.extend([row] * 8)
    return b"".join(out)


def tint_sectors(rgb: bytes, sect: bytes) -> bytes:
    """Alternate sectors tinted, unpainted cells dimmed - on a 1024x1024 RGB."""
    buf = bytearray(rgb)
    for cy in range(64):
        for cx in range(64):
            s = sect[cy * 64 + cx]
            for py in range(16):
                o = ((cy * 16 + py) * 1024 + cx * 16) * 3
                seg = buf[o:o + 48]
                if s == 0x7F:
                    buf[o:o + 48] = bytes(v * 2 // 3 for v in seg)
                elif s & 1:
                    buf[o:o + 48] = bytes(min(255, v + 50) if k % 3 == 0 else v for k, v in enumerate(seg))
                else:
                    buf[o:o + 48] = bytes(min(255, v + 50) if k % 3 == 2 else v for k, v in enumerate(seg))
    return bytes(buf)


def png_write(path, w, h, rgb):
    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ---- the game's own tools ----------------------------------------------------

def tool_path(name: str) -> str:
    for d in ("build-native", "build"):
        p = os.path.join(ROOT, d, name)
        if os.path.exists(p):
            return p
    return ""


def run_ailap(d: str) -> tuple[bool, str]:
    """The ROM's field round the package at three classes (make trackcheck)."""
    exe = tool_path("smk_ailap")
    if not exe:
        return False, "smk_ailap is not built: run `make` in the repository first"
    try:
        r = subprocess.run([exe, ROM_PATH, os.path.abspath(d)], capture_output=True, text=True, timeout=900)
    except subprocess.TimeoutExpired:
        return False, "the field did not finish in 15 minutes"
    return r.returncode == 0, r.stdout + r.stderr


def launch_game(d: str, timetrial: bool = False):
    exe = tool_path("smk")
    if not exe:
        return None
    args = [exe, "--rom", ROM_PATH, "--track", os.path.abspath(d)]
    if timetrial:
        args.append("--timetrial")
    return subprocess.Popen(args, cwd=ROOT)
