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
