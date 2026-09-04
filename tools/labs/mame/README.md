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
* `gpgrid.lua` - a cup's grid from race to race: the order table
  `$010E`, every kart's `$E6` and `$C0` at each start and end, every
  change between races, and a low-WRAM dump per mode change.  `POKE_AT`
  / `SWAP_I` / `SWAP_J` swap two table entries on a frame to prove the
  points play no part (NOTES 275).
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

The recordings that are kept, and what each one settled:

    attack        an AI dropping its weapons - NOTES 190, and the SILENT
                  release measured in NOTES 264
    cc100         a 100cc GP - the class and coin rules (NOTES 173)
    cheep-cheep   Koopa Beach's jumping fish
    choco         Choco Island's dirt and its moles
    crash         wall impacts, and one of make check's replay gates
    flag          a 50cc GP - NOTES 170/171, and the roulette gate
    ghost-valley  a clean five-lap 100cc time trial: the engine rev law
                  end to end (NOTES 265), the drift dust (NOTES 266/268)
                  and the absence of any slide sound (NOTES 267)
    gv, gv1       Ghost Valley blocks; gv1 is a replay gate
    moles         the mole latch and its ride (NOTES 210)
    starts        the three launches (NOTES 143)
    thwomp, win   Thwomp motion; the winner's pose

They are the user's own hands at the pad, and they are committed because
a measurement that cannot be re-run is not a measurement - every number
above can be produced again from these files.

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
