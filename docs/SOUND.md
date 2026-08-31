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

3. Render LONG with ffmpeg (libgme) and cut one clean loop:

       ffmpeg -i tmp/<name>_3000.spc -t 480 tmp/<song>_long.wav
       tools/labs/songcut.py tmp/<song>_long.wav rom/music/<song>.wav

   songcut finds the loop on the loudness envelope, starts the file at a
   phrase boundary and crossfades the seam, so the whole-file loop is
   clean.  A RACE-start snapshot cannot render past ~10 s (the game
   streams data to the driver through the first seconds of a race and
   the .spc has nobody to send it), so race songs are cut from a
   mid-race snapshot and begin at a phrase boundary rather than the
   song's first note; menu-type songs snapshot near their start and loop
   whole.  `key intro.wav loop.wav` in map.txt plays an intro once, then
   loops the second file, when a true intro is wanted.

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

## Sound effects (NOTES 211)

The effects are captured from the running game, not synthesised:

    # one id per run - the subtraction is only clean on the first poke
    SFX_IDS=48 SFX_START=2200 SFX_GAP=180 \
        tools/labs/mame/grab.sh moles tmp/sfxcap/48.wav 45
    SFX_SILENT=1 SFX_IDS=48 SFX_START=2200 SFX_GAP=180 \
        tools/labs/mame/grab.sh moles tmp/sfxcap/base.wav 45
    tools/labs/sfxcut.py tmp/sfxcap/base.wav tmp/sfxcap/48.wav 48 2200 180 rom/sfx

`SFX_QUIET=<id>` pokes an id first (a dead end so far - see NOTES 211).
The port loads `rom/sfx/<ID>.wav` lazily by the GAME'S id, so a call site
reads like the ROM's: `smk_sfx_play(SMK_SFX_BOOST)` is `$80:B48C`'s own
`LDA #$0048 / JSL $81F57A`.  `smk --sfx` plays every captured effect with
its name.  Music is OFF by default now (`N` toggles it, `SMK_MUSIC=1`
starts with it on) so the effects can be judged on their own.

## Rendering the effects from the chip (NOTES 213) - the route that works

Recording the speaker and subtracting leaves the music smeared under
every effect.  This route never records audio:

    # the sample bank: a snapshot of the sound RAM mid-race
    SPC_FRAME=2400 SPC_OUT=tmp/snap \
        tools/labs/mame/replay.sh moles tools/labs/mame/spcdump.lua 60
    tools/labs/brr.py tmp/snap_2400.spc          # decode every BRR sample

    # the notes: every DSP voice, every frame, with and without the poke
    SFX_START=2200 tools/labs/mame/replay.sh moles \
        tools/labs/mame/voicedump.lua 45 > tmp/vd_base.log
    SFX_ID=48 SFX_START=2200 tools/labs/mame/replay.sh moles \
        tools/labs/mame/voicedump.lua 45 > tmp/vdump/48.log

    tools/labs/sfxrender.py tmp/snap_2400.spc tmp/vd_base.log \
        tmp/vdump/48.log rom/sfx/48.wav

The engine is the same idea: it is voice 7 playing SRCN $02 at a pitch
of $4700 + 34*v, so `rom/sfx/engine.wav` is that sample's own loop and
the port steps through it at the rate the DSP would.

## Naming them by ear

`smk --sfx` plays every effect in id order with the name the ROM's own
call site gives it, and asks after each one:

    ENTER = right   i = wrong   r = again   s = skip   q = stop
    ...or just type what it really is

The answers land in `rom/sfx/names.txt` (`SMK_SFX_NAMES` overrides) -
id, length, verdict, name - which is the file to hand back: the naming
in the listener's own words, for the ids the ROM does not name and as a
check on the ones it does.  The engine's sweep is asked about last.
