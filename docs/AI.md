# The AI: what the ROM's opponents do that a player cannot

A reference, distilled from the decode log (`docs/NOTES.md`, the entries
cited) and the user's own recorded races.  Everything here is READ from
the ROM or MEASURED on the running game unless marked OURS; the port
carries all of it except the two items marked at the end.

## Speed

* **The chase row is faster than the class allows the player.**  The
  target-speed table (`$06B0`, four rows by the waypoint attribute) puts
  the chase row at 896 on a fast 50cc sector, where the player's 50cc top
  is 784 plus 80 for ten coins - 864.  At 100cc: 1068 against 992.  Only
  at 150cc does the table fall under the player's 1152.  NOTES 167, 277.
* **The rubber band is aimed at the human.**  `$80ADA0` chooses the row,
  and every branch asks whether the neighbouring kart is the human
  (`$10` bit 15): the fast row when behind the player, a slower one when
  clear ahead.  The catch-up distances (`$80AF0F`) are indexed by the LAP
  and re-tune every lap; a mid-field kart far behind adopts the row of
  the kart ahead, so the pack pulls together.  NOTES 174.
* **Four karts are born faster.**  Blocks 4-7 carry `$DA` = 2, 4, 6, 8 and
  take `$80B099`'s bonus of +4, +8, +16, +0 in place of the rank penalty
  the others pay (`$80B0A1`: 0 to -24).  In every recording they are the
  karts on the player's tail; their maxima read the table to the unit.
  NOTES 277, 278.
* **Surfaces do not slow them.**  The target comes from the sector table
  alone; off-road costs the AI nothing.  NOTES 057.  (The port softens
  this with a cap - OURS, labelled.)
* **The ramp costs them nothing.**  An AI keeps its full speed at a launch
  where the player's takes speed times the cosine of the ramp angle,
  holds it flat through the air, and takes one more boost step on the
  ramp frame.  NOTES 278, 280.
* **The boost pads are theirs too.**  Class `$16` re-arms `$FC` to 32
  every frame and the boost throttle (`$80A5E4`) adds `$32` a frame up to
  `$7E0` - but only in a sector whose attribute is 3, and the state ends
  at a launch.  DK Jr: 901 -> 1380 across Mario Circuit 2's strip.
  NOTES 280.

## Items and coins

* **Weapons from nowhere.**  No item box, no roulette, nothing held.  One
  machine for the whole field (`$80:EEF9`) decides, and the object is
  simply born at the kart: a banana, egg, green shell, poison mushroom or
  fireball by character, or a 300-frame star for Mario and Luigi.
  NOTES 190, 279.
* **A designated victim.**  The machine attacks only the leading human
  slot, only from the victim's second lap, and never another AI.  The
  attacker is the victim's rank-neighbour: behind a leader, ahead of
  anyone else.  A drop is placed only with the victim 16-192 px behind
  and the kart driving straight; a forward throw only with the victim
  leading 48-144 px ahead; the star within 32-64 px.  NOTES 279.
* **No coins to lose.**  `$81E3B8` keeps coins for the two human slots
  only.  An AI's top speed never depends on a coin count and a bump never
  costs it one.  NOTES 275.  (The port's AI coins are OURS, labelled.)

## Handling

* **Steering by row, not by physics.**  The turn rate is a table
  (`$06D0`) by row and heading error: the chase row turns at 128-384 a
  frame where the hold row gets 32-288, so a chasing kart corners better
  for free.  They never hop or drift.  NOTES 041, 277.
* **Never in the air by accident.**  Nothing steers or throttles an
  airborne AI; a flight is a straight line to a clean landing.  NOTES
  280.
* **The teleport was ours.**  The disappearing-and-reappearing karts the
  user saw were the port's own rescue timer misfiring, not a rule of the
  game.  NOTES 169.

## What holds them back

* **The sector table is the ceiling.**  Ten of Bowser Castle 1's 35
  sectors hold the AI to 512 while the player drives 860; Mario Circuit
  2 has one such sector, nine at the top entry, and a pad.  That, not the
  rubber band, is why one course feels easy and the other does not.
  NOTES 280.
* **Attacks are rationed.**  One of the game's two projectile blocks must
  be free (a lost attack still costs the cooldown), 61 frames of
  adjacency, a random draw against a per-character mask by the victim's
  rank (DK Jr and Bowser certain in most ranks, Yoshi one in four, nobody
  against a victim in seventh or eighth), then 180 frames of cooldown for
  the whole field.  NOTES 279.
* **A kart in trouble takes the slowest row** - 8% of the original's
  frames.  NOTES 174.

## Where the port still differs

* The surface immunity is softened by choice (a cap on off-road), and
  the "in trouble" row is approximated from the port's own crash states
  where the game reads `$84` and `$10` bit 5 - the port is slightly
  kinder to the field there than the game.
* The drop's "driving straight" test stands on the port's heading error
  where the game reads the kart's steering word `$2C`; the thrown
  object's arc is the port's until fitted.  NOTES 279.
* The AI's ramp floor (`$2E0`, `$400` on Bowser Castle) is the player's
  reading; no slow AI launch has been recorded.  NOTES 280.

## How each of these was found

`tools/labs/mame/ailog.lua` (every kart every frame), `aidrops.lua` and
`objtrack.lua` (the projectile blocks), `cool.lua` (the attack machine's
own words), `rowlog.lua` with `tools/rowcheck` (the row chooser, gated at
94-97% on the recordings), the oracle rigs in `tools/labs/mc2ramp.py`,
and the user's sessions in `tools/labs/mame/sessions/`: `flag` (50cc),
`cc100`, `cc150`, `attack`, `moles`, `mc2-real`, `mc2`, `mc3`.
