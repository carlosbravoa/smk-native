"""Per-track course data: the sector map, racing-line waypoints, and lap
structure.

Decoded from the loader at $81FBC0-$81FEB5 (docs/NOTES.md 042):

  - word table $81:FF9B + track*2  ->  sector-record stream (bank $C6)
  - word table $81:FFCB + track*2  ->  waypoint stream       (bank $C6)

The record stream builds a 64x64 cell map (16x16 px cells) at $7F:5000 in
which every cell holds a SECTOR index; records are painted in order, one
sector each, terminated by $FF.  The AI steers toward the waypoint of the
sector ahead, and lap/checkpoint logic follows from the sector ordering.

Record format: [type][pos.lo][pos.hi] + payload, where
cell index = pos.lo + (pos.hi << 6).  Types (even bytes; the handler table
at $81FD5D is indexed by the raw type):

  type 0   w, h   rectangle: runs of w cells rightward, h rows downward
  type 2   n      triangle: run right, next row down, width n, n-1, ...
  type 4   n      triangle: run left,  rows down,     shrinking
  type 6   n      triangle: run left,  rows UP,       shrinking
  type 8   n      triangle: run right, rows UP,       shrinking
  type 10  ?, h   column of h cells downward; next column at +63
                  (down-left diagonal), h-1 cells
  type 12  ?, h   as 10 but next column at +65 (down-right diagonal)

Types 10/12 carry a first payload byte the paint loop never reads.

Waypoints: 3 bytes each - x/8, y/8, attribute - count equal to the number
of sector records; the loader then repeats waypoint 0 at the end to close
the loop.  Attributes land at $0800, X at $0900, Y at $0A00.
"""
from __future__ import annotations
from .rom import Rom

TBL_RECORDS  = 0x81FF9B
TBL_WAYPOINTS = 0x81FFCB
TBL_PARAMS   = 0x8180D4      # 6 bytes per track: lap word, strip cell, w1|w2<<8
DATA_BANK = 0xC6
MAP_W = 64
MAP_CELLS = MAP_W * MAP_W
CELL_PX = 16


class CourseError(ValueError):
    pass


def _stream_pc(rom: Rom, table: int, track: int) -> int:
    pc = rom.snes_to_pc(table) + track * 2
    return rom.snes_to_pc((DATA_BANK << 16) | rom.u16(pc))


def build_sector_map(rom: Rom, track: int) -> tuple[bytearray, int]:
    """Paint the 64x64 sector map exactly as $81FC01 does.
    Returns (map, sector_count)."""
    data = rom.data
    p = _stream_pc(rom, TBL_RECORDS, track)
    m = bytearray(MAP_CELLS)
    sector = 0
    for _ in range(1024):
        t = data[p]
        if t == 0xFF:
            return m, sector
        pos = data[p + 1] | ((data[p + 2] & 0xFF) << 6)
        p += 3

        def paint(idx):
            m[idx & (MAP_CELLS - 1)] = sector

        if t == 0:
            w, h = data[p], data[p + 1]; p += 2
            for row in range(h):
                for i in range(w):
                    paint(pos + row * MAP_W + i)
        elif t in (2, 4, 6, 8):
            n = data[p]; p += 1
            xstep = 1 if t in (2, 8) else -1
            ystep = MAP_W if t in (2, 4) else -MAP_W
            base = pos
            while n > 0:
                for i in range(n):
                    paint(base + i * xstep)
                base += ystep
                n -= 1
        elif t in (10, 12):
            _unused, h = data[p], data[p + 1]; p += 2
            step = 63 if t == 10 else 65
            base = pos
            while h > 0:
                for i in range(h):
                    paint(base + i * MAP_W)
                base += step
                h -= 1
        else:
            raise CourseError(f"track {track}: record type {t} at pc ${p-3:X}")
        sector += 1
    raise CourseError(f"track {track}: no stream terminator")


def waypoints(rom: Rom, track: int, count: int):
    """The racing line: (x, y, attr) per sector, plus the closing repeat of
    the first point, exactly as $81FC29 loads it."""
    data = rom.data
    p = _stream_pc(rom, TBL_WAYPOINTS, track)
    pts = []
    for i in range(count):
        pts.append((data[p] * 8, data[p + 1] * 8, data[p + 2]))
        p += 3
    if pts:
        pts.append(pts[0])
    return pts


def mark_finish(rom: Rom, track: int, m: bytearray) -> dict:
    """OR bit 7 over the finish-line rectangle, per $81FC92-$81FCB3:
    width w ($80D8 low) by h rows ($80D8 high), from the cell at $80D6,
    one row (+64) apart.  The rectangle may overhang the track; only cells
    on painted sectors matter to the reader."""
    pc = rom.snes_to_pc(TBL_PARAMS) + track * 6
    lap_word = rom.u16(pc)
    cell = rom.u16(pc + 2)
    w = rom.data[pc + 4]
    h = rom.data[pc + 5]
    for row in range(h):
        for i in range(w):
            m[(cell + row * MAP_W + i) & (MAP_CELLS - 1)] |= 0x80
    return {"lap_word": lap_word, "cell": cell, "w": w, "h": h}


def build_flow_map(m: bytes, pts) -> bytearray:
    """The AI direction field at $7F:4000, exactly as $81FCFC builds it:
    for every on-course cell, the high byte of the angle from the CELL
    CENTRE to the cell's own sector's waypoint (docs/NOTES.md 056).  This
    is why the original AI never steers into walls - the field is derived
    from the racing line over the painted cells only."""
    import math
    flow = bytearray(MAP_CELLS)
    for cell in range(MAP_CELLS):
        sec = m[cell] & 0x7F
        if sec == 0x7F or sec >= len(pts):
            continue
        cx = (cell & 63) * CELL_PX + 8
        cy = (cell >> 6) * CELL_PX + 8
        ang = math.atan2(pts[sec][0] - cx, -(pts[sec][1] - cy))
        a16 = int(ang * 65536 / (2 * math.pi)) & 0xFFFF
        flow[cell] = ((a16 + 0x80) >> 8) & 0xFF      # round to nearest step
    return flow


def sector_at(m: bytes, x: int, y: int) -> int:
    """Low 7 bits are the sector; bit 7 marks the finish strip; $7F means
    off the course entirely ($808931)."""
    return m[((y >> 4) & 63) * MAP_W + ((x >> 4) & 63)]
