# Sound: the music pipeline (NOTES 201/202)

The port plays PRE-RECORDED music (P7's decision): the game's own driver,
snapshotted mid-song and rendered offline. No SPC700 runs in the port.

## Making a song file

1. Record (or reuse) a session that reaches the music you want:

       tools/labs/mame/play.sh <name>          # Esc to finish

   Music lives at: the title, the menus, each course THEME (Mario
   Circuit, Donut Plains, Ghost Valley, Bowser Castle, Choco Island,
   Koopa Beach, Vanilla Lake, Rainbow Road), the results, the GP end.
   The best snapshot moments have no engine note: the countdown, or a
   menu. A snapshot mid-race works too - the engine voices in it decay
   out in the first second.

2. Snapshot the sound CPU at one or more frames of the replay:

       SDL_AUDIODRIVER=dummy SPC_FRAME=900,3000 SPC_OUT=tmp/<name> \
         mame snes -cart rom/smk_usa.sfc \
         -input_directory tools/labs/mame/sessions -playback <name> \
         -state_directory tools/labs/mame/sessions \
         -video none -sound sdl -nothrottle -window -skip_gameinfo \
         -seconds_to_run 300 -autoboot_script tools/labs/mame/spcdump.lua

   (`-sound none` leaves the DSP idle and the snapshot silent; the
   dumper reads the DSP through $F2/$F3 and forces the write-only timer
   control $F1 to 1 - see NOTES 201.)

3. Render with ffmpeg (libgme):

       ffmpeg -i tmp/<name>_3000.spc -t 180 rom/music/<song>.wav

## Mapping songs to states

`rom/music/map.txt` (beside the ROM; git-ignored - nothing derived from
the ROM is committed), one line per state key:

    title      menu.wav
    menu       menu.wav
    results    after_race.wav
    theme0     gv_race.wav        # Ghost Valley
    theme1     mc1_race.wav       # Mario Circuit
    theme2     dp_race.wav        # Donut Plains
    theme3     ci_race.wav        # Choco Island
    theme4     vl_race.wav        # Vanilla Lake
    theme5     kb_race.wav        # Koopa Beach
    theme6     bc_race.wav        # Bowser Castle
    theme7     rr_race.wav        # Rainbow Road

A missing key or file is silence. N toggles the music in the game.

## Still open (S8)

- Loop points (files loop whole; a seam is audible once per pass).
- Every SFX - above all the ENGINE NOTE the turbo launch is timed
  against, then items, hits, the countdown beeps.
- The APU port protocol was tapped (tools/labs/mame/aputap.lua) and is
  layered - command pulses on port 0, streamed blocks on ports 2/3, an
  engine byte on port 1 - so songs are captured from play rather than
  requested by command.
