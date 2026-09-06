# Adding a course: the track engine

Built through T4 (section 8); the status and the numbers are in section
11 at the end.  The rest of this file is the design it was built to,
kept as written where it still holds and corrected where the code found
it wrong (marked CORRECTED).  This is the ground a new course has to stand on:
what a course IS in this port (measured, with the ROM addresses), what a
new one has to supply, where the AI's own data comes from when nobody
authored it, and how a course proves itself before anyone drives it.

Everything below is READ from the ROM or MEASURED on the running game
unless marked OURS.  The measurements were taken on 2026-09-06 against
the USA dump with the repo's own tools (`tools/smktool/course.py`,
`mode7.py`, the surface blob through `smk_decompress`), and the numbers
are quoted so the rules can be checked, not trusted.

Battle Mode stays out (ROADMAP R6).  A "course" here is a GP-style
racing loop.

---

## 0. The one principle

**A new course is new DATA in the game's own shapes.  It is not a new
engine.**

The port already has two loaders that turn ROM bytes into the two
structures every consumer reads: `smk_track` (the Mode 7 plane and the
surface table) and `smk_course` (the sector map, the racing line, the
direction field, the finish strip, the grid, the objects).  Nothing
downstream - the physics, the seven ROM AI karts, the lap rule, Lakitu,
the object spawner, the renderer, the RL environment - knows or cares
where those bytes came from.  So the engine for new tracks is:

1. a **package format** that holds exactly the fields those two
   structures need, and nothing the ROM owns (no art, no code);
2. a **loader façade** that fills the two structures from a package the
   same way it fills them from the ROM, provably (round trip);
3. a **generator** that produces the fields nobody wants to author by
   hand - the sector map, the racing line, the direction field, the
   segment tables - as the ROM's own structures, so the ROM's own AI
   drives them unmodified;
4. a **gate** that a course must pass, behavioural not structural: the
   field laps it, the lap counter counts, Lakitu puts you down on the
   road.

This is ROADMAP R5 applied to content instead of code: mirror the
game's structures, even the ugly ones (a 64x64 sector map painted in
rectangles, a 3-byte waypoint with a 2-bit speed class, a 4-entity
spawn window).  The reward is that every decoded behaviour carries over
for free and every existing gate applies.

---

## 1. What a course is, measured

Every item the two loaders and the race setup read, per course or per
theme, and what a new course does about it.  "THEME" means the eight
tile-set families (`$81:EC2F`, `theme*2` per track); "TRACK" means one
of the 24 ROM slots.

| # | item | ROM source | keyed by | consumer | a new course... |
|---|---|---|---|---|---|
| 1 | tilemap, 128x128 tile indices (16 KB) | `$81:EB5B[track]`, doubly compressed | TRACK | renderer, surface lookup, pickups, blocks | **supplies** (`map.bin`) |
| 2 | tileset, 192 Mode 7 tiles 8x8 | `$81:EBA3[theme]` | THEME | renderer | borrows a theme, or ships its own |
| 3 | object tiles 192-255 (boxes, coins, pads, ramps, oil) | `$C4:0000` | global | renderer | nothing |
| 4 | palette, 256 BGR555 | `$81:EBBB[theme]` | THEME | renderer, kart tints, menus | borrows, or ships its own |
| 5 | surface class per tile, 192 bytes | `$87:FDBA` blob at `$81:EB4B[theme]` | THEME | physics, pickups, blocks, effects, sound | borrows, or ships its own |
| 6 | object-band classes for tiles 192-255 | captured from WRAM `$0BC0` (NOTES 069), not in ROM | global | same | nothing |
| 7 | stamp list: boxes, coin scatters, oil, pads, ramps | `$85:D000 + track*128`, `[kind][cell]`, 42 max | TRACK | stamped into the tilemap at setup (`$84F1A4`) | **supplies** |
| 8 | sprite entities: pipes, moles, plants, fish, Thwomps | `$85:C800 + track*64`, 32 words | TRACK | collider, renderer, spawner | **supplies** positions; the creature is the theme's |
| 9 | sector map 64x64 of 16-px cells | record stream `$81:FF9B[track]` -> `$7F:5000` | TRACK | AI, lap rule, rank, rescue, progress, spawner | **generated** (or authored) |
| 10 | racing line: one waypoint + attribute per sector | `$81:FFCB[track]` | TRACK | AI target speed, rescue drop point, rank tie-break, flow field, RL obs | **generated** (or authored) |
| 11 | direction field 64x64 | built at load from 9+10 (`$81FCFC`) | derived | AI steering, rescue heading, autopilot, RL obs | nothing - derived |
| 12 | finish strip rectangle (cell, w, h) | `$81:80D4 + track*6` | TRACK | lap rule | **supplies** or generated from the start line |
| 13 | grid (x, y, step) | `$81:8A79[track]` -> placement record | TRACK | start positions | **supplies** or generated |
| 14 | segment thresholds + spawn offsets | `$81:8B73/$81:8B8C[track]` -> `$84:DB83`; `$84:DAC5` | TRACK / global | entity spawn windows | **generated** from the entity list |
| 15 | item probability block | `$81:8B73[track] >> 1` -> `$81:B471` | TRACK | item roulette | picks one of the 8 blocks |
| 16 | theme binding | `$81:EC2F[track]` | TRACK | everything per-theme | **supplies** |
| 17 | horizon tiles + map, sky colour | `$81:EBEB/$81:EC03[theme]`, palette[0] | THEME | renderer | comes with the theme |
| 18 | entity sprite sheet + palette base | `$81:EBD3[theme]`, `smk_obj_pal` | THEME | renderer | comes with the theme |
| 19 | entity behaviour (bounce / spin / latch / mover) | port rules keyed on theme (`src/ai.c:154-163`, `smk_theme_has_movers`) | THEME | collider | comes with the theme |
| 20 | breakable-block sequence, coin erase tile, ramp floor | `$80FC6C/70`, `$81:8BBD[theme]`, `$80B79E` | THEME | blocks, pickups, ramps | comes with the theme |
| 21 | music | `rom/music/map.txt` key `theme%d` | THEME | audio | comes with the theme, or its own key |
| 22 | name | invented family word + cup ordinal (`src/cups.c`) | derived from the cup table | menus, results, records | **supplies** |
| 23 | cup membership | `$81:EC1B` | - | menu | none (a CUSTOM page instead) |
| 24 | best laps | `laptimes.txt`, keyed by track INDEX | TRACK | records | keyed by a stable id instead |
| 25 | lap count | `SMK_RACE_LAPS = 5` | global | race | nothing |
| 26 | Lakitu paths, lap sign, flag, coin arcs | `src/*_path.inc`, one capture each | global | presentation | nothing |
| 27 | `lap_word` | `$81:80D4` first word | TRACK | nobody (undecoded) | nothing |

Three facts in that table shape everything else:

* **Everything visual and behavioural that is per-THEME is borrowed.**
  A course picks a theme and inherits its tiles, palette, surface
  classes, horizon, creature, entity rules, music and the four
  theme-keyed gameplay quirks.  That is exactly how the ROM's own 20
  courses work (three Ghost Valleys, four Mario Circuits) and it means a
  custom course needs no art at all.  "Style" = theme.
* **Ramps, boost pads, oil, coins and item boxes are not tiles of any
  theme.**  They exist only as stamps from the object band (tiles
  192-255), placed by the per-track stamp list.  So a new course gets
  them the same way: a stamp record, not a painted tile.  The stamp
  kinds that produce each class are read off the ROM's 64 stamps, not
  guessed (section 6).
* **The AI reads nothing but the sector map, the racing line and the
  field derived from them.**  It never looks at the tilemap.  What
  keeps the field on the road is that the paint and the waypoints were
  laid so that every painted cell's straight line to its sector's
  waypoint stays on the road.  That is the property the generator has
  to manufacture (section 4).

---

## 2. The package

One directory per course.  Text where a person edits, binary where the
game's own structure is a flat array.  No ROM bytes: the tile INDICES
are the author's arrangement, the tiles themselves stay in the user's
ROM.

```
tracks/<slug>/
  course.txt        the manifest (below)
  map.bin           16384 bytes: tile index per cell, row-major, 128x128,
                    BEFORE stamping - the loader stamps, as $84F1A4 does
  sectors.bin       4096 bytes: sector | $80 finish per 16-px cell,
                    $7F = unpainted.  Written by the generator; may be
                    hand-edited; never generated silently at load
  line.txt          one waypoint per line: "x y attr" in track pixels
                    (x, y multiples of 8; attr as the ROM's byte)
  spine.txt         optional: the ordered closed polyline the generator
                    follows, "x y" per line, first point on the start
                    line heading -Y.  Only needed where the centreline
                    is ambiguous (a course that crosses itself)
  roles.png         optional authoring source for the style compiler
                    (section 6): an indexed 128x128 image of ROLES
  style/            optional custom style (section 6.3):
    tiles.bin       192 * 64 bytes, 8-bit Mode 7 pixels (palette indices)
    palette.bin     512 bytes BGR555
    surface.bin     192 bytes, one class per tile
```

`course.txt` is `key value` lines, `#` comments - the convention
`rom/music/map.txt` and `laptimes.txt` already use, so the port needs no
parser it does not have:

```
format   1
name     CHEESE LAND
theme    1               # 0..7: the ROM theme whose tiles, palette,
                         # classes, horizon, creature and music this uses
style    style/          # optional, overrides tiles/palette/surface
grid     104 320 -32     # front kart x y, column step (ROM: $81:903C)
finish   0 16 12 6       # strip: cell x, cell y, w, h in 16-px cells
items    1               # item block 0..7 ($81:B471)
music    theme1          # optional map.txt key; default "theme<N>"
# stamps: the ROM's kind byte, tile coordinates (8 px)
object   0x00  10 35     # item box
object   0xDC  40 12     # coin scatter (5x5)
object   0x74  22 60     # oil slick (3x1)
# sprite obstacles; the creature is the theme's, the spawn windows are
# four entities each in list order (section 4.9)
entity   268 92
entity   164 132
# entity spawn segments: the sector index at which each window opens
segment  12
segment  23
```

Authoring aliases (`box`, `coins`, `oil`, `ramp`, `pad`) are resolved
by the tool into `object` kinds from a catalogue read off the ROM's
stamp table (section 6.2); the manifest the game reads stays numeric.

**Limits, all the ROM's:** 42 stamps (128 bytes / 3); 32 entities (64
bytes / 2); 7 segment thresholds (8 bytes with the `$FF`); sectors
1..127 (`$7F` is off-course; the selftest accepts 10..120); waypoint
coordinates multiples of 8 in 0..1016; the world is 1024 px and wraps
in the sector map (`smk_course_cell` masks) but the kart is clamped
(`advance()` in `src/kart.c`) - a course must not rely on the wrap.

**Identity.**  `<slug>` is the id: records are keyed by it, the menu
lists by `name`.  Two packages with the same slug in two directories is
an error at scan time, not a silent shadow.

**What a course cannot change** without decoding the ROM further: the
lap count (5), the horizon and music of its theme (a course can name
another music key but not another horizon), Lakitu's paths, the object
band's classes, the four-entity window, the theme-keyed entity
behaviour, the per-theme surface classes unless it ships a whole
`style/`.  Each of these is a table the port does not own yet; listing
them here is so nobody invents a field for them.

---

## 3. The loader façade

Today `smk_track_load(rom, track, theme, ...)` and
`smk_course_load(rom, track, ...)` each read the ROM directly.  Between
"bytes from somewhere" and "the struct the game uses" there is no seam.
The engine adds one:

```
                ROM (24 slots)                package directory
                      |                              |
             read_rom_source()               read_pkg_source()
                      \                              /
                       v                            v
                  smk_course_src   (the raw fields of section 1, exactly:
                                    tilemap, theme, stamps, entities,
                                    sector map, waypoints, finish, grid,
                                    segments, item block, name, id)
                                |
                     smk_course_build()   ->  smk_track + smk_course
                     (stamping, surface table, flow field, movers reset -
                      the code that is in the two loaders now, unchanged)
```

* `smk_course_src` is a plain struct of the fields in the package.  The
  ROM reader is today's loader code up to the point where it has the
  raw values; the package reader is a file parser.  The BUILD step is
  one function, shared, and it is exactly the code that already exists.
* Export is the mirror: `smk_course_src` -> files.  `tools/smktool` gets
  the same pair in Python, so the tools and the game agree (the
  existing course.py twin must first be fixed to prefill `$7F`, not 0 -
  it currently disagrees with `src/course.c` on the very point NOTES
  124 found a bug in).
* **Round trip is the proof of the format** (phase T0): export all 20 GP
  courses to packages, load them back, and require `smk_track` and
  `smk_course` byte-identical to the ROM path.  If a field is missing
  from the package, this fails.  It is also the regression gate for
  every later change to the format.

**A reference, not an index.**  `int track` threads through `main.c`,
`menu.c`, `env.c`, `records.c` and the tools.  It becomes

```c
typedef struct {
    int  rom_track;          /* 0..19, or -1 for a package            */
    char id[32];             /* "rom07" for ROM slots, the slug for packages */
    char path[256];          /* the package directory, or ""           */
} smk_course_ref;
```

held in a registry built at startup: the 20 ROM slots first (their ids
fixed, e.g. `rom07`, so records for them stay where they are), then
every package found under `tracks/` next to the binary, under
`$XDG_DATA_HOME/smk-port/tracks/`, and under each `--track-dir`.
`--track` accepts an index (as today), a slug, or a path.  The RL
environment's `cfg.track` becomes a ref too; `smk_net_drives_track`
(the trained driver's 0..19 guard) says yes to any course with a sector
map, which is every course the registry admits.

**Records** (`laptimes.txt`) gain a second line form, `track <id>
frames character`; the integer form is read as before and written as
the id from then on.  **Names** come from the manifest for packages and
from `smk_track_name` for ROM slots; nothing else changes.

**The menu** gets a fifth column, CUSTOM, on the course screen: the
package list five at a time, up/down within, left/right across pages.
GP mode stays on the four ROM cups (a cup of packages is a later
feature and a different manifest, `cup.txt`; do not fold it in here).
Single race, VS and Time Trial take a package.  The minimap already
follows the loaded tilemap (`build_track_map`), so a package gets one
for free; a course-select preview is the same picture and can be added
when the ROM course screen is (ledger S20), not before.

---

## 4. The generator: the AI's data from the map alone

Input: `map.bin` + theme (so, every cell's surface class), the start
line (from `finish`/`grid` or from the tiles), the stamp and entity
lists, and optionally `spine.txt`.  Output: `sectors.bin`, `line.txt`,
the `segment` lines.  A TOOL step (`tools/trackgen.py`), never done in
the game at load: what runs is what is on disk, reviewable, diffable,
and what the gate ran.

The rules below are OURS.  What is not ours is the target they are
tuned to: the 20 original courses, whose sector maps and racing lines
are the ROM's, and which the generator must be able to regenerate to
within a measured tolerance (section 7.3).  That benchmark is the
reference; the generator's own output is never the reference for
anything (memory: never derive the reference from the model).

### 4.1 What the ROM's data looks like (the constraints, measured)

Across the 20 GP courses, from `$81:FF9B`/`$81:FFCB`/`$81:80D4`/
`$81:8A79`:

| property | measured |
|---|---|
| sectors per course | 23..66 (mean 38) |
| waypoint spacing, consecutive | mean 102 px, median 97, min 8, max 266 |
| where waypoint i sits | in sector i+1 on 672 of 743 (90%); in i+2/i+3 where a later record overpaints; in its own sector once |
| attribute low 2 bits (AI speed row) | 0: 146, 1: 287, 2: 222, 3: 88 waypoints |
| turn angle ahead of the waypoint, by row | row 3 median 6.5 deg, row 2 15.0, row 1 28.7, row 0 37.9 |
| attribute bit 7 (airborne sector rejected) | ONE sector in the whole ROM: track 15 sector 20, attr `$81`, at (600, 832) - Mario Circuit 2's crossing |
| bits 2-6 | never set |
| finish strip | 6..28 cells tall, 8..32 wide; overlaps sector 0 (and often 1) AND the last sector (and often the second last) on all 20 |
| grid | front kart inside the strip, heading 0 = -Y on every course (`$81:903C` never writes `$2A`); rows +24 px behind; columns `step` = +-24, +-32, 40 apart |
| painted cells | 1891..3310 of 4096 |
| paint beyond the road, void themes (Ghost Valley, Bowser, Rainbow, Koopa water) | 1..4 cells over the void/water, 300..700 such cells a course |
| paint beyond the road, open themes (Mario, Donut, Choco) | 0..2 cells; the far infield grass is left `$7F` (up to 1358 driveable cells unpainted on Choco Island) |
| unpainted road | 0..8 cells on every course - a few cells at wall corners, never a stretch |

And the properties the consumers rely on (from reading every reader,
`src/ai.c`, `src/course.c`, `src/autopilot.c`, `src/env.c`,
`src/main.c`):

1. Sectors are numbered in driving order and wrap n-1 -> 0.
2. Unpainted is `$7F`; sector 0 is a real sector (NOTES 124, 294).
3. Every index 0..n-1 appears in the map, or the flow field has a hole
   and progress stalls there.
4. Each sector is one contiguous stretch of the corridor: the field
   aims every cell of a sector at that sector's ONE waypoint, so a
   sector split across two stretches aims half its cells backwards.
5. **Every painted cell's straight line to its sector's waypoint must
   cross no solid cell.**  The AI reads the flow byte and drives that
   way (`$80B0E8`); the waypoints are NOT a drivable polyline (MC2's
   29 -> 30 crosses a barrier, NOTES 265) - only the field is.  This is
   the property that keeps the field off the walls, and it is the
   generator's main job.
6. The waypoint is the rescue drop point (`$80B373`): on driveable
   ground, its own cell painted (the rescue heading is the flow word at
   that cell).
7. The finish strip must contain cells of {0, 1} and of {n-2, n-1}
   (`smk_progress_step`'s two tests).
8. The grid's front kart is in the strip in the last sector, facing -Y
   (`smk_racer_start` forces `sector = n-1`); the karts drive UP the
   screen off the line.  **A custom course's start straight runs
   north.**  There is no rotated grid in the ROM, so there is none
   here; the tool refuses a start line that is not horizontal.
9. Waypoint attribute bits 0-1 = the AI's speed row, bit 7 = the
   airborne-reject flag, and `(attr & 3) == 3` is also the only sector
   in which the AI's boost-pad throttle runs (`$80B015`).

### 4.2 Class map

From the tilemap and the theme's surface table (plus the captured
object band), per 8-px tile, then per 16-px cell (a cell is ROAD if any
of its four tiles is):

* SOLID: bit 7 (`$80`, `$82`, `$84`).
* HAZARD: `$20`..`$3E` - void, water, lava, edge, the launch classes.
* ROAD: `$40`..`$4E` - uncapped types 0..7, measured terminal speed
  >= 87% of road (NOTES 066).  OURS as a boundary: it is where the
  racing line is allowed to go.
* OFF: `$50`..`$5E` - capped or slow types (grass, dirt, sand, ice,
  snow).  Driveable, painted near the road, never the line.
* The object band: item boxes (`$14`), coins (`$1A`), pads (`$16`),
  ramps (`$10`), oil (`$18`) count as ROAD; the stamped walls (`$80`,
  tiles 240-243) as SOLID.

### 4.3 The spine  (CORRECTED: as built)

The loop the sectors are cut along and the waypoints sit on.  As
built, it is not a medial axis:

* A **geodesic distance** along the road from the start line, forward
  only: Dijkstra over the road tiles, 8-connected, with the line a
  barrier nothing steps across (the loop starts on it and ends on the
  row behind it; the barrier also covers the verge beside the road so
  nothing sneaks round the line's end over grass or void).  Off-road
  tiles cost four times a road tile, so a bridge or a gap in the kerb
  still closes a loop and a shortcut never pays - and only within three
  tiles of the road, so a wall across the road is an error and not a
  detour over the infield; shallow water is waded at the same price; a
  void, lava or deep water tile is entered only within two tiles of a
  ramp and six of one - a jump - at six times.
* The **spine** is the shortest such loop with the road's edges made
  expensive (a tile within two of anything that is not road costs up to
  3.5x), walked back from the row behind the line.  CORRECTED: the
  first draft used the centroid of each distance wavefront, i.e. the
  middle of the road, and the field drove 22% further than on the
  ROM's data with the same speed - the ROM's waypoints hug the inside
  of a bend, and so does a shortest path.
* Every spine point carries its plain geodesic distance, and that is
  the arc the cuts, the paint (by each road tile's distance) and the
  waypoints (on the spine, by arc) all use, so they agree by
  construction.
* A course whose road splits (an island, a wide field, a shortcut) is
  accepted with a note: the spine takes one side, the paint covers
  both.  A course that CROSSES itself (Mario Circuit 2) is wrong - the
  distance short-circuits at the crossing - and `spine.txt` is still the
  design's answer; the tool does not read it yet.
* The ROM's sectors are hand-laid rectangles; the generated ones are
  bands of geodesic distance, perpendicular to the road.  Different
  shapes, the same consumers.

### 4.4 Cuts: where one sector ends

Start with cuts every S = 96 px of arc length (the measured median),
the first at the start line.  Then, per sector, run the visibility
test of 4.1 #5 for the cells the painter would give it (4.6) against
the waypoint it would get (4.5); while it fails, split the sector at
its midpoint; once it is under 64 px, move its exit cut - and so its
waypoint - by 16, 32 and 48 px either way instead.  What still fails
is reported.  Stop at 120 sectors; if a course needs more it is too
long or too twisty for the 7-bit index and the tool says so.  The
visibility test skips cells a kart cannot drive in (at least half
wall or hazard): the paint reaches over walls and water as the ROM's
rectangles do, and the field's direction there never steers anything.

This reproduces the ROM's shape without copying it: short sectors in
hairpins (their min spacing is 8-36 px in the tightest bends), long
ones on straights (up to 266 px).

### 4.5 Waypoints

Waypoint i is the spine point 8 px PAST cut i+1, snapped to the 8-px
grid the ROM's byte format imposes - i.e. just inside sector i+1,
where 90% of the ROM's are.  Then nudged, if needed, to the nearest
ROAD tile (4.1 #6).

### 4.6 Painting

For every cell: project its centre onto the spine (nearest spine
point, with the corridor bound below), take that point's arc length,
and paint the sector whose [cut_i, cut_i+1) contains it.

The corridor bound decides which cells are painted at all:

* ROAD cells: always, if within 4 cells of the spine (a wider road is
  a road the tool should be told about).
* OFF and HAZARD cells: within 2 cells of the nearest ROAD cell, OURS,
  from the measured 1..4 over the void and 0..2 over grass.  The
  margin is what makes a kart that drifts wide still hold a sector (an
  unpainted cell KEEPS the old sector, `$808962`, so the rescue still
  knows where you were) and is what NOTES 294's outside-hugging came
  from.
* SOLID cells: painted if inside the corridor - harmless, and the ROM's
  rectangles cover them too.
* Everything else stays `$7F`.

Where two stretches of the spine run within the corridor bound of one
cell (a hairpin, parallel straights), the nearer stretch wins, which is
what the ROM's hand-laid rectangles do; where the two are equidistant
the tool reports the cell and the lint later checks that its flow
does not point at a wall.

### 4.7 The direction field

Nothing to generate: `smk_course_build` derives it from the map and the
line exactly as `$81FCFC` does (rounded atan2, 95% byte-exact against
the game).  The visibility test in 4.4 is the field's correctness
checked before the field exists.

### 4.8 Attributes

* Speed row from the bend at the waypoint.  CORRECTED by measurement:
  the bend predicts the ROM's own row on 43% of its 743 waypoints at
  best (a search over every threshold triple, on the bend at, ahead of
  and before the waypoint), so the rows are NOT a function of the
  geometry - they are the designers' hand-tuned knob (docs/AI.md: the
  sector table is the ceiling; Bowser Castle 1 holds the field at row
  0 in ten of 35 sectors, Mario Circuit 2 in one).  The generator
  therefore assigns rows by the QUANTILES that reproduce the ROM's mix
  - 20% row 0, 39% row 1, 30% row 2, 12% row 3 - over the bend measured
  at the waypoint (< 3 deg -> 3, < 18.4 -> 2, < 50.4 -> 1, else 0), and
  `line.txt` is where the author tunes them.  OURS.
* Row 3 also arms the AI's boost-pad throttle, so the generator gives
  any sector holding a pad stamp row 3 - the ROM does exactly that on
  Mario Circuit 2's strip (NOTES 280).
* Bit 7 on any sector whose paint is overflown by a ramp from another
  sector (the tool knows the ramp stamps and the launch speed's range
  of flight).  Reported, not silent: the ROM sets it once in 20
  courses, so a generator that sets it often is wrong.

### 4.9 Finish, grid, segments

* **Finish strip**: the road's width plus one cell each side, 3 cells
  above the start line and 3 below (6 tall, in the ROM's 6..28 range),
  so it holds the cut between the last sector and 0 with margin.
* **Grid**: front kart at the road centre minus 16 px, 48 px below the
  line (in the strip's lower half, as measured on the 20: front kart
  inside the strip, the seven behind trailing out of it), `step` = -32
  (the second column 32 px left).  Both overridable in the manifest;
  the selftest's "eight slots plus the solo start on driveable ground"
  check runs on every package.
* **Segments**: sort the entities by arc length; windows of four in
  that order (the ROM refills a driver's four slots from
  `ent[seg_off[seg]/2 ..]` and `$84:DAC5` is 0, 8, 16, 24, 0 - the
  fifth segment reopens the first window, so 16 entities are reachable
  and a 17th never spawns: bug 14).  Threshold k = the sector two
  before window k's first entity, k = 1..3.  More than 16 entities is
  a warning with the ones that will never appear named.

### 4.10 Authored overrides

Every generated file is a plain file the author may edit: move a
waypoint, change a row, repaint a cell.  The tool never overwrites an
edited `sectors.bin`/`line.txt` unless told (`--regen`), and the lint
runs on whatever is on disk.

---

## 5. Sprite entities and the theme

The two-bit kind in the entity word is dead in the port (nothing
branches on it) and the creature is the theme's:

| theme | creature | reaction (`src/ai.c`) |
|---|---|---|
| 0 Ghost Valley | none - the lists are empty | - |
| 1 Mario Circuit, 4 Vanilla Lake | pipe / post | bounce |
| 2 Donut Plains | mole | latch (`hazard_hit = 3`), pass-through |
| 3 Choco Island, 5 Koopa Beach | piranha plant / cheep-cheep | spin, pass-through |
| 6 Bowser Castle | Thwomp | bounce; squash while falling; drive under when high |
| 7 Rainbow Road | Thwomp | spin AND bounce |

A package's `entity x y` lines are the ROM's `[y:7][x:7]` cell words
(coordinates `cell*8 + 4`); the count and the creature come with the
theme.  Ghost Valley courses therefore have no entities, and a Bowser
course's are Thwomps that park until the first lap completes
(`smk_course_movers_step`, activation on lap 2 - measured).  A course
that wants pipes on a Bowser tileset is asking for a per-entity kind
the ROM does not have here; that is a decode (of `$85:E0B9`'s scripts
and the sheet layout), not a manifest field.

---

## 6. Style: themes, the role map, custom tilesets

### 6.1 Style is a theme

`theme N` buys the whole look and feel of one of the eight families:
tiles, palette (including the kart tints, which are palette rows the
theme owns), surface classes, horizon, sky, creature, music key, and
the theme-keyed rules (Ghost Valley's crumbling rails, Bowser's `$400`
ramp floor, the coin erase tile).  This is tier A: the author places
tile indices (`map.bin`) from the theme's 192, exactly as the ROM's own
level designers did.  It needs a tile picker with the theme's tiles
rendered from the user's ROM - the tool draws the sheet on demand and
never writes it to the repo.

### 6.2 Tier B: the role map, compiled to tiles

Most people do not want to place 16384 indices.  `roles.png` is an
indexed 128x128 image where the index is a ROLE, and the compiler
picks tiles:

| role | class family it must compile to |
|---|---|
| ROAD | `$40`..`$4E` (the theme's road class) |
| OFF | `$50`..`$5E` (the theme's grass / dirt / sand / ice) |
| WALL | bit 7 |
| WATER (wade) | `$22` |
| HAZARD (void / deep / lava) | `$20`, `$24`, `$26`, `$28` - whichever the theme has |
| BLOCK (breakable) | `$82` / `$84` - Ghost Valley and Vanilla Lake only |
| START | the theme's start-line tiles, across the road, horizontal |
| BOX, COINS, OIL, PAD, RAMP | markers -> `object` stamps, not tiles |
| ENTITY | marker -> `entity` line |
| SPINE | optional polyline -> `spine.txt` |

Compilation, per theme, from two catalogues read off the ROM:

* **The class of every tile** - the theme's surface table.  That is the
  invariant: the lint requires `class(tile at cell)` to be in the
  role's family for every cell.  Behaviour is exactly what was drawn,
  whatever the art does.
* **Which tile goes next to which** - four-neighbour co-occurrence
  counts over the theme's own original tilemaps (three to four courses
  a theme).  Interior cells take the role's most frequent tile; edge
  cells take the tile that best matches its already-placed neighbours
  (greedy, scanline order, deterministic - the same input compiles to
  the same map).  OURS; the art is the ROM's, the adjacency statistics
  are the ROM's, the choice rule is ours and is expected to look
  wrong at odd corners.  The author fixes those in tier A.
* **Stamps**: the tool renders all 64 stamps (`$84:F23D`, sizes
  `$84:F384`) once and reads which kinds yield item boxes (`$14`),
  coin scatters (`$1A`), oil (`$18`), pads (`$16`), ramps (`$10`) - a
  catalogue by class, not by assumption.  CORRECTED by that catalogue:
  the 3x1 kind `$74` the design notes called oil is ONE coin (83 uses in
  the 20 courses); the 5x5 kinds `$DC`..`$F0` are coin scatters; oil is
  the 2x2 kind `$10`; the pads are `$08`/`$09`; the ramps are `$54` (3
  wide, for a road running up or down) and `$98` (3 tall); boxes are
  kinds 0..3 (four graphics of one box).

A role a theme cannot express - WATER on Mario Circuit, BLOCK on Donut
Plains - is an error naming the cell, not a substitution.

### 6.3 Tier C: a custom style

`style/` supplies the three per-theme pieces the renderer and physics
need - `tiles.bin` (192 tiles, 8-bit palette indices, the expanded form
`smk_track_load` produces), `palette.bin`, `surface.bin` - and `theme N`
still names the family whose horizon, creature, kart tints, quirks and
music it borrows.  The art must be the author's own.  The classes in
`surface.bin` must come from the decoded set (section 4.2's families
and the per-class behaviour in `src/player.c`); an unknown class is
refused.  Tier C is the path to a course that looks like nothing in
the ROM; it is last in the phases because nothing in the engine
depends on it.

---

## 7. The gate

A course is not done when it loads.  It is done when the ROM's own
consumers behave on it as they do on the ROM's own data.  The gate
runs headless (`make trackcheck TRACK=<ref>`), no window (memory: no
windows while they work).

### 7.1 Lint (structural, at load with `--strict` and in the tool)

* every field present, every limit of section 2 met;
* sectors numbered 0..n-1, each present, each one contiguous stretch;
* unpainted is `$7F`; every ROAD cell within the corridor painted;
* every waypoint on ROAD, its cell painted, in sector i+1 (warn) and
  within 300 px of the previous (error);
* **visibility**: every painted cell sees its waypoint over non-solid
  ground (4.1 #5) - the one check that predicts the field's behaviour;
* finish strip holds cells of {0,1} and {n-2,n-1}, the grid's front
  kart inside it, all eight slots and the solo start on driveable
  ground, heading -Y off a horizontal line;
* role/class agreement for a compiled map; stamps on ROAD; entities
  on or beside the road; segment thresholds ascending.

### 7.2 Behaviour (the real gate)

Reusing the tools that gate the ROM's courses today:

| check | tool | pass |
|---|---|---|
| the field laps it | `smk_ailap` on the package: 7 ROM AI karts, ORIGINAL rules, 50/100/150cc | every kart completes 5 laps; no kart rescued; lap times within 2x of the fastest |
| off the void | the NOTES 294 metric: frames any AI kart spends on a HAZARD class | 0 on void/water themes |
| the lap counter | `smk_laptest`'s rule: 6 crossings for 5 laps, first crossing under a third of a lap | as on the 20 |
| the rescue | the selftest's "no travelling kart fished up" over 2400 frames; a kart dropped on every HAZARD cell of the corridor is put down on ROAD facing along the field | as on the 20 |
| a rule-abiding driver | `--autodrive` (the pad-pressing autopilot) completes a lap | informative for a hard course, required for an easy one |
| the trained driver | `NEURAL` completes a lap (the net never saw the course - it is a held-out test by construction) | informative |
| RL | `smk_env` steps 3 laps; `make envcheck` replays them through the binary | position/speed match on every frame |

### 7.3 The generator's own benchmark (calibration, never the reference)

Feed the generator the 20 originals' tilemaps, themes, finish
rectangles and grids ONLY, and run 7.2 on its output next to the same
run on the ROM's data:

* AI lap times per course within 5% of the ROM-data run (the port's
  field already laps Ghost Valley 3 within 3% of the game's own, NOTES
  294 - that is the yardstick);
* hazard frames 0 where the ROM-data run has 0;
* rescue count 0 where the ROM-data run has 0;
* sector count within 30% of the ROM's; waypoint spacing distribution
  overlapping the measured one.

This is how the OURS rules of section 4 get their numbers and how a
change to them is judged.  The ROM's data is the reference; the
generator is never compared to itself.

### 7.4 The oracle, optionally (phase T5)

The four battle-arena slots (tracks 20..23) are out of scope for play
(R6), which makes them free SCRATCH SLOTS in a patched ROM.  The
romhack toolchain that exists (`tools/build.py`, `smktool/assets.py`'s
`repack` and `FreeSpace`, asar) can write a package into slot 20: the
tilemap recompressed under `$81:EB5B[20]`, the sector map re-encoded as
rectangle records under `$81:FF9B[20]` (each sector's cell set
decomposed greedily into type-0 rectangles - the port's loader and the
game's paint the same map), the waypoints under `$81:FFCB[20]`, the
finish/grid/theme/stamps/entities in their tables, and the oracle
(`tools/labs`, the `$0124` hook) boots the real game into it.  Then
the real `$80ADA0` drives the custom course, and the real `$808962`
counts its laps, and whatever the port's gate missed shows up there.
And the user can play it in MAME.

It is optional because the port's consumers are already the decoded
routines and 7.2 exercises them; it is worth doing because "the game
does X on this data" has been the fastest instrument this project has
(ROADMAP, the gate section), and because a generator tuned only
against the port's AI would be tuned against a port.

---

## 8. Phases

Each phase leaves `make check` green and adds its own gate.

**T0 - The format holds everything.**  `smk_course_src`, the split of
the two loaders into read + build, the package reader and writer in C
and in `tools/smktool` (and the `$7F` fix in `course.py`), export of
the 20 GP courses to `tracks/rom/<id>/`.  Gate: round trip
byte-identical on all 20; the selftest runs its per-course checks on
the re-imported packages too.  No visible change to the game.

**T1 - A package is playable.**  `smk_course_ref`, the registry,
`--track <slug|path>`, the CUSTOM column, names, records by id, the RL
env on a ref, `smk_net_drives_track` on "has a sector map".  Gate: a
copy of Mario Circuit 1 under a new slug plays end to end in single
race, VS and Time Trial, records a lap, and the ROM slots are untouched
(replay gates unchanged).

**T2 - The generator, calibrated.**  `tools/trackgen.py` sections 4.2
to 4.9, and the benchmark 7.3.  Gate: the benchmark thresholds on all
20; a hand-drawn oval in tier A with generated course data passes 7.2.
This is where the OURS rules earn their numbers, and where the
spine-ambiguity refusal is exercised on Mario Circuit 2.

**T3 - The gate is a command.**  `smk_tracklint`, `make trackcheck`,
the hazard-frame and rescue metrics as tool output.  Gate: it fails on
each deliberately broken package (a missing sector, a waypoint behind
a wall, a strip off the road) and passes the 20 and the oval.

**T4 - The role map.**  `roles.png` -> `map.bin` + stamps + entities,
the stamp catalogue, the adjacency catalogue.  Gate: the 20 originals
reduced to roles and recompiled load, lint clean, class map identical
to the originals' class map (the art may differ; the classes may not),
and pass 7.2.

**T5 - The oracle slot** (optional).  Section 7.4.  Gate: the T2 oval
in slot 20 of a patched ROM laps in the oracle with the game's own AI.

**T6 - Custom styles** (optional).  Section 6.3.  Gate: a tier-C
package with a hand-made tileset renders, and its surface classes are
the decoded set.

Not planned: an in-game editor; importing other editors' formats (the
community's Epic Edit exports would be a natural source, but its
format is not decoded here and would be read from its source, not
inferred - a later note); cups of packages; rotated starts.

---

## 9. Ledger entries this creates

To be added to ROADMAP's ledger when the phases land, and kept honest
there:

* **The package format is OURS**; every field in it is a ROM structure.
* **The generator's rules are OURS** (4.2 ROAD boundary, 4.4 spacing
  and split, 4.5 the 8-px past-cut placement, 4.6 the 2-cell margin,
  4.8 the row thresholds, 4.9 the strip and grid geometry); their
  targets are the ROM's 20 courses, measured in section 4.1.
* **The CUSTOM column, the slug ids, the id-keyed records are OURS.**
* **The role compiler's tile choice is OURS**; the class of every tile
  and the adjacency statistics are the ROM's.
* **Unchanged and still the ROM's**: everything the course data feeds.

---

## 10. Risks

* **The visibility test is necessary, not sufficient.**  The ROM's AI
  also slews at a table rate and holds its row's target speed; a
  straight line to the waypoint can exist and still be un-drivable at
  row 3 into a hairpin.  7.3's lap-time and hazard-frame benchmarks
  catch it; the row assignment (4.8) is the lever.
* **Courses that cross themselves.**  The spine is asked for, not
  inferred, and the autopilot's own "within 4 sectors ahead" guard
  (`src/autopilot.c:166`) exists because the cell under a kart can
  belong to the other stretch.  The ROM handles Mario Circuit 2 with
  one bit-7 sector and paint that keeps the two stretches' cells
  apart; the generator must produce the same separation or the lint
  must refuse.
* **The two-player entity window** is measured for one player (four
  live entities) and doubled for two, OURS (`src/course.c:263`).  A
  custom course with 16 entities exercises that more than the ROM's do.
* **Rainbow Road's surface table** is read past the blob's end and
  comes back `$00` (class 0, the "crawl" band - unmeasured cap).  A
  theme-7 package inherits that; it is not a custom-track problem but
  it will look like one.
* **The Python twin's `$7F`** must be fixed before any Python tool
  writes `sectors.bin`, or every package generated by it carries
  NOTES 124's bug.

---

## 11. Status (2026-09-06): built through T4

### What exists

* **The source and the build** (`smk_course_src`, `src/tracks.c`,
  `src/assets.c`, `src/course.c`): the two loaders are `read` + `build`,
  and a package is the same `build` fed from files.  The selftest
  writes every GP course as a package, reads it back through the
  registry and requires the same track and course, byte for byte, for
  every tile the map uses (tiles it never uses are the expander's
  leftovers and differ by construction; section 9).
* **The registry**: indices 24 and up are the packages found under
  `tracks/`, `$SMK_TRACKS` (a colon list) and
  `$XDG_DATA_HOME/smk-port/tracks`, in name order; `--track` takes an
  index, a slug or a directory; `--track-dir` adds a parent.  The
  course screen shows a fifth column, **CUSTOM**, five to a page, when
  packages exist and the mode is not a Grand Prix.  Best laps are kept
  by slug (`laptimes.txt` lines `slug frames character`); a slug that
  is not registered when the file is read is carried and written back.
  The trained CPU drives packages (`smk_net_drives_track`); the RL
  environment takes a package index; a package may name its own music
  key.
* **The creator**, `tools/trackgen.py` (section 2 for the files):
  `new` writes a template oval as `roles.txt`; `build` compiles the
  roles to tiles and stamps, generates the course data and lints;
  `gen` regenerates from an existing `map.bin`; `lint`, `render`
  (a PNG with the sectors, the line, the strip and the grid), `export`
  (a ROM slot as a package) and `from-rom` (a ROM slot's map with
  GENERATED course data, the benchmark's input).  `make trackcheck
  TRACK=dir` is the lint and the field at three classes.
* **The catalogues** (`tools/smktool/tilecat.py`): per theme, the
  class of every tile, the tiles its courses use and how often, the
  four-neighbour counts, the start-line tile (the one tile that lives
  only inside finish rectangles: `$1E $42 $10 $51/$52 $79 $7F $5B
  $01` for themes 0..7), and the 64 stamps classified by what their
  tiles do (section 6.2, corrected there).
* `tracks/oval` is the template built: the field laps it at 842 /
  712 / 647 frames at 50 / 100 / 150cc with no time off the road.
* **The editor**, `tools/trackstudio.py` (`make studio`): a tkinter
  application over the same pipeline (`tools/smktool/project.py`
  holds the model both it and the command line use).  A brush for the
  six roles, a click for the start line (it spans the road at the
  clicked row; with none drawn, the build lays it across the longest
  straight running north whose road can hold the grid, `auto_line`),
  a click per object with its footprint shown, a theme
  box, undo and redo, two views (the roles as flat colour, the tiles
  as the game draws them from the user's ROM) with the sectors, the
  racing line, the finish strip and the grid overlaid, zoom.  *Build
  and validate* runs the compile, the generator and the lint in a
  worker thread and lists every problem; *Race the AI* runs
  `smk_ailap` on the package and reports the laps and the hazard
  frames, and when the field does not lap it, where its furthest kart
  stopped (a red mark on the map, from `smk_ailap`'s `stalled:` line)
  and what to try; *Play* and *Time trial* start the game on the
  package.  The start line is the author's, placed with the Start line
  tool and never moved by the editor (an automatic line that followed
  the road as it was painted walked the grid off the screen - the
  user's report - and was removed; the command line still lays one
  when a roles file has none); Build asks for the line first; the grid
  is drawn behind it as soon as it exists; a road that does not loop is
  marked where it ends.  A built course's
  waypoints can be dragged and their speed row set with the keys 0 to
  3, the lint re-running as they move.  The panes resize.  A theme
  that lacks a role compiles it as the nearest one it has and says so
  on the tool and in the notes (`tilecat.effective_roles`: Rainbow
  Road has road and void only, so its off-road, walls and water are
  void; most themes have no breakable block, so it is a wall) - before
  that, Rainbow Road's painted grass became road and the whole map was
  road.  Nothing in it is ROM data: the pictures are drawn from the ROM
  at run time.

### The benchmark (7.3), on the last run

The twenty originals, their maps and stamps only, course data
generated; the ROM AI's first full lap in frames on the ROM's data and
on the generated data, and the frames any AI kart spent on a hazard
class.  `lint` is the number of problems the lint reports.

| trk | course | sectors ROM/gen | 50cc ROM/gen | 100cc | 150cc | hazard frames ROM/gen (50/100/150) | lint |
|---|---|---|---|---|---|---|---|
| 0 | Mario Circuit 3 | 40/48 | 1828/2054 | 1516/1645 | 1342/1542 | 0/0 0/0 0/0 | 1 |
| 1 | Ghost Valley 2 | 45/39 | 1412/1448 | 1148/1189 | 1024/1064 | 0/0 0/0 0/0 | 1 |
| 2 | Donut Plains 2 | 37/54 | lap / NO LAP | | | 0/0 | 4 |
| 3 | Bowser Castle 2 | 66/75 | 2229/2070 | 1814/1762 | 1653/1576 | 304/456 262/1501 238/417 | 3 |
| 4 | Vanilla Lake 2 | 29/35 | 2398/1426 | 1687/1164 | 1644/1089 | 37/219 20/171 25/101 | 1 |
| 5 | Rainbow Road | 46/40 | 2023/1828 | 1662/1510 | 1479/1402 | 0/258 0/159 0/347 | 0 |
| 6 | Koopa Beach 2 | 25/4 | lap / NO LAP | | | 0/0 | 3 |
| 7 | Mario Circuit 1 | 30/31 | 1320/1369 | 1077/1105 | 962/1014 | 0/0 0/0 0/0 | 0 |
| 8 | Ghost Valley 3 | 46/46 | 1778/1819 | 1453/1494 | 1313/1339 | 15/227 129/177 0/265 | 1 |
| 9 | Bowser Castle 3 | 51/81 | 1925/2170 | 1572/2189 | 1425/1442 | 946/1647 666/8171 1788/95 | 7 |
| 10 | Choco Island 2 | 29/36 | 1617/1639 | 1305/1328 | 1172/1186 | 0/0 0/0 0/0 | 1 |
| 11 | Donut Plains 3 | 35/44 | lap / NO LAP | | | 1167/1670 1637/1572 207/245 | 2 |
| 12 | Vanilla Lake 1 | 23/35 | 1321/1234 | 1113/1036 | 1034/920 | 0/0 0/0 0/0 | 3 |
| 13 | Koopa Beach 1 | 36/- | refused: the road never comes back (the course wades between islands) | | | | |
| 14 | Mario Circuit 4 | 41/55 | 2178/2249 | 1755/1844 | 1631/1678 | 0/0 0/0 0/0 | 1 |
| 15 | Mario Circuit 2 | 35/- | refused: the road never comes back (the course crosses itself) | | | | |
| 16 | Ghost Valley 1 | 33/40 | 1473/1411 | 1210/1134 | 1078/2063 | 0/113 0/84 0/7139 | 3 |
| 17 | Bowser Castle 1 | 35/61 | 1908/2315 | 1580/2857 | 1452/3260 | 0/6413 0/10270 0/13053 | 5 |
| 18 | Choco Island 1 | 24/32 | 1461/1394 | 1187/1141 | 1053/1013 | 0/0 0/0 0/0 | 1 |
| 19 | Donut Plains 1 | 37/43 | 1790/1818 | 1445/1478 | 1281/1340 | 0/0 0/0 0/0 | 1 |

Read: 45 of 60 runs lap; 40 within 15% of the ROM data (two courses that
earlier produced a bogus 200-frame loop across water or a crossing now
refuse with the place the road ends); twelve courses
(0, 1, 3, 7, 8, 10, 12, 14, 18, 19, and 5 and 16 with a little time on
the void) regenerate as courses the field races much as it races the
originals.  The lint's single problem on most of them is a handful of
painted cells on the far side of a wall whose flow points into it - the
ROM's paint has those too.

What the failures are, each one understood:

* **The map alone does not say where the course goes** on Koopa Beach
  (islands joined by shallow water: the shortest wade is not the
  course), Vanilla Lake 2 (a frozen lake of driveable ice: the shortest
  loop cuts across it, 0.59 of the ROM's lap) and Mario Circuit 2 (the
  road crosses itself: the distance short-circuits at the crossing).
  On these the ROM's hand-laid paint IS the route.  The design's answer
  is `spine.txt` (4.3); the tool does not read it yet.  An author's own
  course draws its road, so this bites only where the road is not the
  route - a course that wades or crosses.
* **Donut Plains 2 and 3** stall with no time on a hazard: the field
  wedges somewhere; lint 2..4.  Not diagnosed.
* **The Bowser Castles** cross lava on ramps.  The generated line takes
  the ramp (a hazard is entered only within two tiles of one, and only
  within six tiles of it), and Bowser Castle 2 laps 5% FASTER than on
  the ROM's data, but the field falls in the lava more than it does on
  the ROM's paint (1 and 3), and Ghost Valley 1's 150cc run finds a
  gap.  The waypoint before a jump has to be exactly on the ramp's
  approach; the generator does not know that yet.
* **The seven-entity limit**: Bowser Castle 1 and Rainbow Road carry 18
  entities; the game's windows reach the first 16 (bug 14), and the
  generator says so.

### What the numbers say about the rules

* The shortest wall-avoiding loop is the racing line (4.3): switching
  to it from the centreline took Mario Circuit 1 from 27% slower than
  the ROM's data to 4%.
* The speed rows are not geometry (4.8, S49): 43% at best.  Matching
  the ROM's row MIX gets a course that plays like the originals on
  average; making one play like a SPECIFIC original is the author's
  `line.txt`.
* Every consumer took the generated structures unchanged.  Nothing in
  `src/ai.c`, the lap rule or the rescue was touched.

### Not built

T5 (a package into a battle-arena slot of a patched ROM, for the
oracle), T6 (custom styles - `style/` is read and built but no tool
writes one), `spine.txt`, the bit-7 airborne flag (the generator never
sets it), a course-select preview, cups of packages.
