# MAME as the oracle

MAME (`mame snes -cart rom/smk_usa.sfc`) runs the cart with a real DSP-1
(`upd7725` + `dsp1.bin`) at ~350% headless, and its Lua and debugger give
per-frame memory access.  It replaced the Python CPU for the physics decode
(NOTES 103).  What works and what does not, learned the hard way:

* `-video none -sound none -nothrottle -window -autoboot_script X.lua`
  runs headless; `manager.machine:exit()` ends the run.  The attract race
  (mode `$36 == 2`) starts about 3000 frames after boot and lasts ~1750.
* **Lua memory taps only see the bank `$00-$3F` / `$80-$BF` mirrors of
  low WRAM.**  Reads or writes that the game makes through bank `$7E`
  (`sta $0710,y` with DB=$7E, the long pointer `[$04]`, the replay's pad
  store) never reach a tap, whatever range you install it on.  Reading
  `mem:read_u16(0x7E....)` at `frame_done` is fine; forcing values is not
  (a `mem:write` at frame end is overwritten by the next NMI).
* **The debugger's watchpoints match the exact address the CPU used** -
  set them on `00xxxx`, `7exxxx` AND `81xxxx` (setup code runs with
  DB=$81).  `-debug -debugscript file -debuglog` writes `printf` output to
  `debug.log`; `pc` in a watchpoint action is the NEXT instruction.
  `bpset` never fired in this build (0.285); use `wpset` on the
  instruction's operand instead.
* Memory-mapped I/O taps ($2180, $420B) work for DMA/WMDATA hunting.

Files:

* `demolog.lua` - logs both demo karts' kart fields every frame to CSV,
  prints the `$0710` per-player block at race start.
* `resim.py` - re-simulates the decoded player physics from the logged
  pad words and diffs it against the log (0 mismatches expected outside
  the mushroom frames).
* `camlog.lua` - the camera azimuth `$94` against the kart's angles.
* `watch_setup.txt` - the debugscript that found the block writer.
* `demo_race.csv` - the full per-frame log of the attract race (both
  karts) that `tools/demoreplay.c` replays as the accuracy gate in
  `make check` (NOTES 107).  Regenerate with `DEMOLOG=... demolog.lua`.

* `multidemo.lua` - the attract loop runs SEVERAL races (NOTES 113): it
  writes one CSV per race plus `demos.txt` (frame, mode, track,
  characters).  Demo 1 is the 2P GP on track 7, demo 2 DK alone on track
  19 in time trial, demo 3 Peach/Yoshi on track 18.
* `demo_tt_track19.csv`, `demo_gp_track18.csv` - two of those, kept for
  the replay tool (`smk_demoreplay rom.sfc <csv> 1000`).
* `pix.lua` - dumps a real frame's pixels to a PPM headlessly
  (`screen:pixels()`), the ground truth for anything visual.
* `hdma.lua` - the live HDMA channel setup at a race frame; this is how
  the BGMODE split above the horizon was found (NOTES 114).
* `vshadow.lua` - a VRAM/CGRAM shadow built from the write and DMA
  stream.  INCOMPLETE: it sees only one 1 KB upload, so the bulk VRAM
  traffic goes through a path the `$420B` tap misses.

## Recording a real session (the player's own hands)

Some things the rigs cannot reach - breaking a Ghost Valley block, a
proper lap of a track the attract mode never shows - are trivial for a
human with a pad.  `play.sh` records such a session so it can be replayed
HERE, deterministically, with any instrumentation attached:

    tools/labs/mame/play.sh gv        # play; Esc when done
    tools/labs/mame/replay.sh gv tools/labs/mame/watch_blocks.lua 180

`play.sh` writes the input recording and any save states into
`tools/labs/mame/sessions/`.  In game, Shift+F7 then 1 saves a state
(park it just before the interesting moment), F7 then 1 loads it back.

`replay.sh` runs the recording headless with a Lua script attached, so
the same moment can be watched as many times as needed with different
watches.  `watch_blocks.lua` reports any change in the live tilemap with
the kart's position and state - which is exactly what a block vanishing
should look like.
