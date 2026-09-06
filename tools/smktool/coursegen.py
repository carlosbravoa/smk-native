"""The course generator: the AI's own data from the map alone
(docs/TRACKS.md section 4).

Input: a tilemap and its theme (so every tile's class), the start line
(a horizontal run of LINE cells the karts drive UP from), the stamps and
entities.  Output: the 64x64 sector map, one waypoint and attribute per
sector, the finish strip, the grid and the entity spawn segments - the
ROM's structures, so the ROM's AI, lap rule and rescue drive them.

The rules are OURS.  Their targets are the twenty original courses,
measured in docs/TRACKS.md 4.1: waypoints ~97 px apart and 8 px into
the next sector, paint 2 cells past the road, every painted cell's
straight line to its waypoint over non-solid ground (the property the
flow field needs, since the AI drives the field and not the waypoints),
the speed row from the turn ahead at the measured medians.
"""
from __future__ import annotations
import heapq, math
from . import surface as S

W = 128                   # tiles per side
CW = 64                   # 16-px cells per side
SPACING = 96.0            # px of arc per sector to start with (median 97)
MIN_SECTOR = 32.0
MAX_SECTORS = 120
MARGIN = 2                # cells of paint past the road (measured 1..4 over the void, 0..2 over grass)
# The AI speed row from the bend at the waypoint.  MEASURED: the bend
# predicts the ROM's own row on 43% of its 743 waypoints at best - the
# rows are a hand-tuned knob, not a law of the geometry (docs/AI.md: the
# sector table is the ceiling).  So the thresholds are the QUANTILES that
# reproduce the ROM's row mix (20% row 0, 39% row 1, 30% row 2, 12% row
# 3) over the bend measured at its waypoints; the author's line.txt is
# the real knob.
ROW_TURN = (3.0, 18.4, 50.4)   # deg: < -> row 3, 2, 1, else 0


class GenError(Exception):
    pass


def _wrap_angle(d):
    while d > math.pi: d -= 2 * math.pi
    while d < -math.pi: d += 2 * math.pi
    return d


class Course:
    """Everything the generator derives, kept for the lint and the render."""
    def __init__(self):
        self.sect = bytearray([0x7F]) * (CW * CW)
        self.line = []          # (x, y, attr)
        self.finish = (0, 0, 0, 0)
        self.grid = (0, 0, 0)
        self.segments = []
        self.spine = []         # (arc, x, y) in px
        self.lap_px = 0.0
        self.notes = []


def classes_of(tm: bytes, cls: bytes) -> list[int]:
    return [cls[t] for t in tm]


def find_line_cells(tm: bytes, line_tiles: list[int]) -> list[int]:
    """The start line as drawn: the theme's line tiles, one horizontal run."""
    want = set(line_tiles)
    cells = [i for i in range(W * W) if tm[i] in want]
    return cells


def generate(tm: bytes, cls: bytes, line_cells: list[int], stamps: list, ents: list,
             stamp_cat: dict) -> Course:
    """tm: 16384 tile indices AFTER stamping (so pads and ramps are classes);
    cls: 256 class bytes; line_cells: tile indices of the start line;
    stamps: (kind, col, row); ents: (col, row, kindbits)."""
    out = Course()
    cm = classes_of(tm, cls)
    road = [S.is_road(c) for c in cm]
    hazard = [S.is_hazard(c) for c in cm]
    solid = [S.is_solid(c) for c in cm]
    drive = [S.is_driveable(c) for c in cm]
    if not line_cells:
        raise GenError("no start line: paint a horizontal run of S on the road")
    ys = {i // W for i in line_cells}
    if len(ys) != 1:
        raise GenError("the start line must be ONE horizontal row of cells (rows %s)" % sorted(ys))
    ly = ys.pop()
    lxs = sorted(i % W for i in line_cells)
    if lxs[-1] - lxs[0] + 1 != len(lxs):
        raise GenError("the start line must be one unbroken run")
    for i in line_cells:
        road[i] = True                     # the line's tiles are road by definition

    # --- geodesic distance along the road from the line, forward (-Y) only ---
    INF = float("inf")
    behind = {(ly + 1) * W + x for x in lxs}          # the row the karts start on
    lineset = set(line_cells)
    # the line is a barrier across the whole road AND past its ends, so
    # nothing sneaks round it over the grass or the void beside the road
    # ...but not into another stretch of road that happens to pass beside
    # the start straight (Bowser Castle): the barrier extends over grass,
    # void and wall only, up to 12 tiles
    # (the drawn line may be narrower than the road - Koopa Beach's is - so
    # the road it sits on is covered first, then the verge)
    def extend(x, step):
        gap = False
        for _ in range(W):
            nx = x + step
            if nx < 0 or nx >= W:
                break
            if road[ly * W + nx]:
                if gap:
                    break                # another stretch beyond the verge
            else:
                gap = True
                if abs(nx - (lxs[-1] if step > 0 else lxs[0])) > 12:
                    break
            x = nx
        return x
    bar_x0, bar_x1 = extend(lxs[0], -1), extend(lxs[-1], 1)
    def crosses_line(i, j):
        yi, yj = i // W, j // W
        xi, xj = i % W, j % W
        if not (bar_x0 <= xi <= bar_x1 and bar_x0 <= xj <= bar_x1):
            return False
        return (yi <= ly < yj) or (yj <= ly < yi)
    # a jump starts on a ramp tile and the flight carries over the road
    # beside it before the gap (Bowser Castle: ramp, one road tile, lava),
    # so a hazard is entered from anything within two tiles of a ramp
    ramp0 = [c in (0x10, 0x12, 0x1C, 0x2A, 0x2C) for c in cm]
    ramp = list(ramp0)
    for i in range(W * W):
        if not ramp0[i]:
            continue
        x, y = i % W, i // W
        for yy in range(max(0, y - 2), min(W, y + 3)):
            for xx in range(max(0, x - 2), min(W, x + 3)):
                ramp[yy * W + xx] = True
    wade = [c == 0x22 for c in cm]              # shallow water: driven through, slowly
    # the soft pass may leave the road, but never far from it: a bridge or
    # a gap in the kerb is within three tiles of road on its way, a field
    # is not (a wall across the road produced a detour over the infield)
    near_road = [False] * (W * W)
    for i in range(W * W):
        if not road[i]:
            continue
        x, y = i % W, i // W
        for yy in range(max(0, y - 3), min(W, y + 4)):
            for xx in range(max(0, x - 3), min(W, x + 4)):
                near_road[yy * W + xx] = True
    # ...and the flight has a length: void, lava or deep water further than
    # six tiles from a ramp is never crossed (Rainbow Road's void beside
    # its ramps was a shortcut across the map before this)
    jumpable = [False] * (W * W)
    for i in range(W * W):
        if not ramp0[i]:
            continue
        x, y = i % W, i // W
        for yy in range(max(0, y - 6), min(W, y + 7)):
            for xx in range(max(0, x - 6), min(W, x + 7)):
                jumpable[yy * W + xx] = True
    # how far a road tile is from anything that is not road (Chebyshev, 0..3)
    edge = [3] * (W * W)
    for i in range(W * W):
        if not road[i]:
            continue
        x, y = i % W, i // W
        for r in (1, 2, 3):
            hit = False
            for yy in range(y - r, y + r + 1):
                for xx in range(x - r, x + r + 1):
                    if not (0 <= xx < W and 0 <= yy < W) or not road[yy * W + xx]:
                        hit = True; break
                if hit: break
            if hit:
                edge[i] = r - 1; break

    def dijkstra(penalised, soft):
        """soft: grass, dirt and shallow water may be crossed (at a price);
        the road alone is tried first, so a lake or a lawn beside the
        course is never a way round the line."""
        dist = [INF] * (W * W)
        prev = [-1] * (W * W)
        pq = []
        for i in line_cells:
            dist[i] = 0.0
            heapq.heappush(pq, (0.0, i))
        while pq:
            d, i = heapq.heappop(pq)
            if d > dist[i]:
                continue
            x, y = i % W, i // W
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    if not dx and not dy:
                        continue
                    nx, ny = x + dx, y + dy
                    if nx < 0 or ny < 0 or nx >= W or ny >= W:
                        continue
                    j = ny * W + nx
                    if solid[j]:
                        continue
                    # a hazard is crossed only in the air: from a ramp, or
                    # continuing a flight; grass and dirt at a cost, so a
                    # course whose loop needs them (a bridge, a gap in the
                    # kerb) still closes and a shortcut never pays
                    if hazard[j] and not wade[j] and not ((ramp[i] or hazard[i]) and jumpable[j]):
                        continue
                    if not road[j] and not (hazard[j] and not wade[j]) and not (soft and near_road[j]):
                        continue
                    if not road[j] and not hazard[j] and not drive[j]:
                        continue
                    # the line is never stepped across: the loop starts on it
                    # and ends on the row behind it
                    if crosses_line(i, j):
                        continue
                    if dx and dy and (solid[y * W + nx] or solid[ny * W + x]):
                        continue                     # no cutting a solid corner
                    step = 1.4142135 if dx and dy else 1.0
                    if hazard[j]:
                        step *= 4.0 if wade[j] else 6.0   # water is waded; a gap is jumped, and only where no road goes round
                    elif not road[j]:
                        step *= 4.0      # off the road: never a shortcut, sometimes the only way
                    if penalised:
                        # the racing line keeps two tiles off the edge where it can:
                        # the ROM's waypoints hug the inside of a bend, not the wall
                        step *= 1.0 + 2.5 * (2 - edge[j]) if edge[j] < 2 else 1.0
                    nd = d + step
                    if nd < dist[j]:
                        dist[j] = nd
                        prev[j] = i
                        heapq.heappush(pq, (nd, j))
        return dist, prev

    dist, _ = dijkstra(False, False)
    back = [i for i in behind if dist[i] < INF]
    soft = False
    if not back:
        # the road alone does not close: let it wade and cross the verge
        soft = True
        dist, _ = dijkstra(False, True)
        back = [i for i in behind if dist[i] < INF]
        if back:
            out.notes.append("the loop closes only over grass, dirt or shallow water somewhere")
    pdist, pprev = dijkstra(True, soft)
    reach = [i for i in range(W * W) if dist[i] < INF and road[i]]
    if len(reach) < 64:
        raise GenError("the road from the start line reaches only %d tiles" % len(reach))
    if not back:
        far = max(reach, key=lambda i: dist[i])
        fx, fy = far % W, far // W
        around = sorted({S.kind(cm[(fy + dy) * W + fx + dx]) for dy in (-2, -1, 0, 1, 2) for dx in (-2, -1, 0, 1, 2)
                         if 0 <= fx + dx < W and 0 <= fy + dy < W})
        raise GenError("the road never comes back to the start line from behind: not a loop "
                       "(the farthest it reaches is tile %d,%d at %d px, among %s; the row behind "
                       "the line is %d, x %d..%d)" % (fx, fy, dist[far] * 8, ", ".join(around), ly + 1, lxs[0], lxs[-1]))
    lap_tiles = max(dist[i] for i in back)
    out.lap_px = lap_tiles * 8.0

    # --- one road, one wavefront: two well-separated components at the
    # same distance mean the course forks or crosses itself (4.3) --------
    STEP = 4.0
    forks = []
    nb = int(lap_tiles / STEP) + 1
    buckets = [[] for _ in range(nb + 1)]
    for i in reach:
        b = int(dist[i] / STEP)
        if b <= nb:
            buckets[b].append(i)
    for b in range(nb + 1):
        cells = buckets[b]
        if not cells:
            continue
        comp = _components(cells)
        big = [c for c in comp if len(c) >= 3]
        if len(big) > 1:
            (ax, ay), (bx, by) = _centroid(big[0]), _centroid(big[1])
            if math.hypot(ax - bx, ay - by) > 12 and not forks:
                forks.append("the road splits at arc %d px (around tiles %d,%d and %d,%d): "
                             "the line takes one side, the paint covers both" % (b * STEP * 8, ax, ay, bx, by))

    # --- the spine: the shortest loop, two tiles off the walls, walked back
    # from the row behind the line; each point carries its plain geodesic
    # distance so cuts and paint (by that distance) and waypoints (on the
    # spine) agree ---------------------------------------------------------
    end = min(back, key=lambda i: pdist[i])
    path = []
    i = end
    while i >= 0:
        path.append(i)
        i = pprev[i]
    path.reverse()                                   # from the line round to behind it
    spine = []
    for i in path:
        spine.append((dist[i] * 8.0, (i % W) * 8.0 + 4.0, (i // W) * 8.0 + 4.0))
    # each point carries its tile's own distance, so a waypoint placed at an
    # arc lands in the sector that arc paints; the inside-hugging path can
    # run a tile or two out of order, hence the sort
    spine.sort()
    out.spine = spine
    out.notes.extend(forks)
    lap_px = out.lap_px

    arcs = [a for a, _, _ in spine]

    def spine_at(arc):
        arc = arc % lap_px
        import bisect
        lo = max(0, bisect.bisect_right(arcs, arc) - 1)
        hi = (lo + 1) % len(spine)
        a0, x0, y0 = spine[lo]
        a1, x1, y1 = spine[hi]
        if hi == 0:
            a1 += lap_px
        span = a1 - a0 if a1 > a0 else 1.0
        t = max(0.0, min(1.0, (arc - a0) / span))
        return x0 + (x1 - x0) * t, y0 + (y1 - y0) * t

    def heading_at(arc):
        x0, y0 = spine_at(arc - 12)
        x1, y1 = spine_at(arc + 12)
        return math.atan2(x1 - x0, -(y1 - y0))

    # --- the nearest road tile to a point, as a waypoint (tile corner, /8) ---
    road_list = reach

    def snap(px, py):
        tx, ty = int(px // 8), int(py // 8)
        for want in (road, drive):
            best, bd = None, 1e9
            for y in range(max(0, ty - 4), min(W, ty + 5)):
                for x in range(max(0, tx - 4), min(W, tx + 5)):
                    i = y * W + x
                    if want[i] and not solid[i]:
                        d = (x * 8 + 4 - px) ** 2 + (y * 8 + 4 - py) ** 2
                        if d < bd:
                            bd, best = d, (x, y)
            if best is not None:
                return best
        return (tx, ty)

    # --- cuts, waypoints, paint; split what the field could not see -------
    cuts = [0.0]
    while cuts[-1] + SPACING * 1.5 < lap_px:
        cuts.append(cuts[-1] + SPACING)

    def cell_of_tile(i):
        return (i // W // 2) * CW + (i % W) // 2

    shifted = {}
    shifts = {}
    for _round in range(400):
        n = len(cuts)
        if n > MAX_SECTORS:
            raise GenError("more than %d sectors: the course is too long or too twisty" % MAX_SECTORS)
        # waypoint i: 8 px past cut i+1, on the road
        wps = []
        for i in range(n):
            arc = (cuts[(i + 1) % n] if i + 1 < n else lap_px) + 8.0
            wx, wy = spine_at(arc)
            wps.append(snap(wx, wy))
        # sector of a road tile by its arc
        def sector_of_arc(a):
            s = 0
            while s + 1 < n and a >= cuts[s + 1]:
                s += 1
            return s
        tile_sector = [-1] * (W * W)
        for i in reach:
            tile_sector[i] = sector_of_arc(dist[i] * 8.0)
        # paint: a cell takes the smallest-arc road tile in it; margin cells
        # the nearest painted road cell within MARGIN
        sect = bytearray([0x7F]) * (CW * CW)
        cell_arc = {}
        for i in reach:
            c = cell_of_tile(i)
            a = dist[i] * 8.0
            if c not in cell_arc or a < cell_arc[c]:
                cell_arc[c] = a
        for c, a in cell_arc.items():
            sect[c] = sector_of_arc(a)
        painted = dict(cell_arc)
        for _m in range(MARGIN):
            new = {}
            for c in list(painted):
                cx, cy = c % CW, c // CW
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < CW and 0 <= ny < CW:
                            j = ny * CW + nx
                            if j not in painted and j not in new:
                                new[j] = painted[c]
                            elif j in new and painted[c] < new[j]:
                                new[j] = painted[c]
            for j, a in new.items():
                sect[j] = sector_of_arc(a)
            painted.update(new)
        # a waypoint's own cell belongs to the NEXT sector, as 90% of the
        # ROM's do: the rescue puts a kart down at the waypoint facing the
        # field there, and a cell aimed at a point inside itself points
        # north (the user: Lakitu never faced the right way)
        for i in range(n):
            c = wps[i][1] // 2 * CW + wps[i][0] // 2
            if sect[c] == i:
                sect[c] = (i + 1) % n
        # visibility: every painted cell to its waypoint over non-solid tiles
        bad = set()
        for c in painted:
            if not _stands(c, drive, solid):
                continue                 # paint over a wall or the water, as the ROM's; no kart drives there
            s = sect[c]
            wx, wy = wps[s][0] * 8 + 4, wps[s][1] * 8 + 4
            cx, cy = (c % CW) * 16 + 8, (c // CW) * 16 + 8
            if not _visible(cx, cy, wx, wy, solid):
                bad.add(s)
        if not bad:
            break
        # split every failing sector that is long enough; a short one
        # moves its exit (and so its waypoint) forward instead, then back
        changed = False
        newcuts = []
        for s in range(n):
            a0 = cuts[s]
            a1 = cuts[s + 1] if s + 1 < n else lap_px
            newcuts.append(a0)
            if s in bad and a1 - a0 >= 2 * MIN_SECTOR:
                newcuts.append((a0 + a1) / 2)
                changed = True
        if changed:
            cuts = newcuts
            continue
        shifts.setdefault(_round, 0)
        for s in sorted(bad):
            k = s + 1
            if k >= n:
                continue
            tried = shifted.get(s, 0)
            if tried >= 6:
                continue
            delta = (16.0, -16.0, 32.0, -32.0, 48.0, -48.0)[tried]
            lo = cuts[s] + MIN_SECTOR
            hi = (cuts[k + 1] if k + 1 < n else lap_px) - MIN_SECTOR
            moved = cuts[k] + delta
            shifted[s] = tried + 1
            if lo < moved < hi:
                cuts[k] = moved
                changed = True
                break
        if not changed:
            out.notes.append("%d sector(s) still hide their waypoint behind a wall: %s"
                             % (len(bad), sorted(bad)))
            break
    out.sect = sect
    n = len(cuts)

    # --- attributes: the speed row from the turn ahead, row 3 on a pad ----
    pad_sectors = set()
    for i in reach:
        if cm[i] == 0x16:
            pad_sectors.add(tile_sector[i])
    line = []
    for i in range(n):
        arc = (cuts[(i + 1) % n] if i + 1 < n else lap_px) + 8.0
        h0 = heading_at(arc - SPACING / 2)
        h1 = heading_at(arc + SPACING / 2)
        turn = abs(_wrap_angle(h1 - h0)) * 180 / math.pi
        row = 3 if turn < ROW_TURN[0] else 2 if turn < ROW_TURN[1] else 1 if turn < ROW_TURN[2] else 0
        if i in pad_sectors:
            row = 3
        line.append((wps[i][0] * 8, wps[i][1] * 8, row))
    out.line = line

    # --- the finish strip and the grid ------------------------------------
    out.finish, out.grid = start_geometry(line_cells)

    # --- entity spawn windows: four at a time along the lap ---------------
    if ents:
        def arc_of(col, row):
            best, bd = 0.0, 1e9
            for y in range(max(0, row - 3), min(W, row + 4)):
                for x in range(max(0, col - 3), min(W, col + 4)):
                    i = y * W + x
                    if dist[i] < INF:
                        d = (x - col) ** 2 + (y - row) ** 2
                        if d < bd:
                            bd, best = d, dist[i] * 8.0
            return best
        order = sorted(range(len(ents)), key=lambda k: arc_of(ents[k][0], ents[k][1]))
        out.ent_order = order
        segs = []
        for k in range(4, min(len(ents), 16), 4):
            a = arc_of(ents[order[k]][0], ents[order[k]][1])
            s = 0
            while s + 1 < n and a >= cuts[s + 1]:
                s += 1
            s = max(1, s - 2)
            if segs and s <= segs[-1]:
                s = segs[-1] + 1
            segs.append(s)
        out.segments = [s for s in segs if s < n]
        if len(ents) > 16:
            out.notes.append("%d entities: the game's windows reach the first 16 only" % len(ents))
    return out


def start_geometry(line_cells):
    """The finish strip and the grid from the start line: the strip is the
    road's width plus a cell each side, two cells above the line and
    three below; the front kart is 40 px behind the line at the road's
    centre, the columns 32 px apart, the rows 24 px back (4.9)."""
    ly = line_cells[0] // W
    lxs = sorted(i % W for i in line_cells)
    lcy = ly // 2
    cx0 = max(0, lxs[0] // 2 - 1)
    cx1 = min(CW - 1, lxs[-1] // 2 + 1)
    fy0 = max(0, lcy - 2)
    finish = (cx0, fy0, cx1 - cx0 + 1, min(6, CW - fy0))
    centre = (lxs[0] + lxs[-1] + 1) * 8 // 2
    grid = (centre - 16, ly * 8 + 40, 32)
    return finish, grid


def _stands(c, drive, solid):
    """A 16-px cell a kart can drive in: at least half driveable tiles.  The
    paint reaches over walls and water as the ROM's rectangles do, but the
    field's direction only matters where a kart is under its own power."""
    tx, ty = (c % CW) * 2, (c // CW) * 2
    return sum(drive[(ty + dy) * W + tx + dx] and not solid[(ty + dy) * W + tx + dx]
               for dy in (0, 1) for dx in (0, 1)) >= 2


def _components(cells):
    cs = set(cells)
    seen = set()
    comps = []
    for c in cells:
        if c in seen:
            continue
        comp = []
        stack = [c]
        seen.add(c)
        while stack:
            i = stack.pop()
            comp.append(i)
            x, y = i % W, i // W
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    j = (y + dy) * W + (x + dx)
                    if 0 <= x + dx < W and 0 <= y + dy < W and j in cs and j not in seen:
                        seen.add(j)
                        stack.append(j)
        comps.append(comp)
    comps.sort(key=len, reverse=True)
    return comps


def _centroid(cells):
    n = len(cells)
    return sum(i % W for i in cells) / n, sum(i // W for i in cells) / n


def _visible(x0, y0, x1, y1, solid):
    """A straight line between two points crosses no solid tile.  The
    first 12 px are not tested: a cell half inside a wall (the paint
    reaches over walls, as the ROM's rectangles do) still counts as
    seeing out of it, since a kart is never in the wall's own tiles."""
    length = max(abs(x1 - x0), abs(y1 - y0))
    steps = int(length / 4) + 1
    for k in range(steps + 1):
        t = k / steps
        if t * length < 12:
            continue
        x = int((x0 + (x1 - x0) * t) // 8)
        y = int((y0 + (y1 - y0) * t) // 8)
        if 0 <= x < W and 0 <= y < W and solid[y * W + x]:
            return False
    return True


# ---- the lint -----------------------------------------------------------

def lint(tm: bytes, cls: bytes, sect: bytes, line: list, finish: tuple, grid: tuple,
         ents: list) -> list[str]:
    """What must hold for the consumers (docs/TRACKS.md 7.1).  Returns
    problems; an empty list is a pass."""
    p = []
    cm = classes_of(tm, cls)
    n = len(line)
    if n < 1 or n > 127:
        p.append("sectors: %d, need 1..127" % n)
        return p
    present = [False] * n
    for c in range(CW * CW):
        s = sect[c]
        if s == 0x7F:
            continue
        if s >= n:
            p.append("cell %d names sector %d of %d" % (c, s, n))
            return p
        present[s] = True
    missing = [s for s in range(n) if not present[s]]
    if missing:
        p.append("sectors with no cells: %s" % missing[:10])
    solid = [S.is_solid(c) for c in cm]
    drive = [S.is_driveable(c) for c in cm]
    for s, (x, y, a) in enumerate(line):
        if x % 8 or y % 8:
            p.append("waypoint %d is not on the 8-px grid" % s)
        c = cm[(y // 8) * W + x // 8]
        if not S.is_driveable(c):
            p.append("waypoint %d at (%d,%d) is not on driveable ground (class $%02X)" % (s, x, y, c))
        cell = (y // 16) * CW + x // 16
        if sect[cell] == 0x7F:
            p.append("waypoint %d's cell is unpainted (the rescue heading reads it)" % s)
        elif sect[cell] == s:
            p.append("waypoint %d sits in its own sector %d: Lakitu would put a kart down there facing north, "
                     "whatever the road does (the cell must belong to sector %d)" % (s, s, (s + 1) % n))
        elif sect[cell] not in ((s + 1) % n, (s + 2) % n):
            p.append("waypoint %d lies in sector %d, not in %d or %d" % (s, sect[cell], s, (s + 1) % n))
        if a & 0x7C:
            p.append("waypoint %d attribute $%02X uses bits the game never does" % (s, a))
    # visibility
    hidden = 0
    for c in range(CW * CW):
        s = sect[c]
        if s == 0x7F or not _stands(c, drive, solid):
            continue
        wx, wy = line[s][0] + 4, line[s][1] + 4
        if not _visible((c % CW) * 16 + 8, (c // CW) * 16 + 8, wx, wy, solid):
            hidden += 1
    if hidden:
        p.append("%d painted cells cannot see their waypoint over solid ground" % hidden)
    # the finish strip must hold both ends of the loop
    fx, fy, fw, fh = finish
    ends_a = ends_b = False
    for row in range(fh):
        for i in range(fw):
            s = sect[((fy + row) * CW + fx + i) & 4095]
            if s in (0, 1):
                ends_a = True
            if s in (n - 1, n - 2):
                ends_b = True
    if not (ends_a and ends_b):
        p.append("the finish strip must cover cells of sectors {0,1} and {%d,%d}" % (n - 2, n - 1))
    # the grid: eight karts on driveable ground, inside the loop's last sector
    gx, gy, gs = grid
    for slot in range(8):
        x = gx + (gs if slot & 1 else 0)
        y = gy + 24 * slot
        if not (0 <= x < 1024 and 0 <= y < 1024):
            p.append("grid slot %d is off the map" % slot)
            continue
        c = cm[(y // 8) * W + x // 8]
        if not S.is_driveable(c):
            p.append("grid slot %d at (%d,%d) is not on driveable ground (class $%02X)" % (slot, x, y, c))
    front = sect[(gy // 16) * CW + gx // 16]
    if front != n - 1:
        p.append("the front of the grid is in sector %s, not the last (%d)" % (front, n - 1))
    if not (sect[(gy // 16) * CW + gx // 16] == n - 1 and fy <= gy // 16 < fy + fh and fx <= gx // 16 < fx + fw):
        p.append("the front kart is not inside the finish strip")
    for k, (col, row, _) in enumerate(ents):
        if col < 0 or col > 127 or row < 0 or row > 127:
            p.append("entity %d is off the map" % k)
    return p
