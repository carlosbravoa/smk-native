#!/bin/sh
# Record a recorded session's audio while sfxgrab.lua pokes sounds.
#   tools/labs/mame/grab.sh <session> <out.wav> <seconds>
set -e
# never open a window: a MAME run must not steal the user's focus
SDL_VIDEODRIVER=dummy; export SDL_VIDEODRIVER
cd "$(dirname "$0")/../../.."
NAME="$1"; OUT="$2"; SECS="${3:-320}"
SDL_AUDIODRIVER=dummy exec mame snes -cart rom/smk_usa.sfc \
     -input_directory tools/labs/mame/sessions -playback "$NAME" \
     -state_directory tools/labs/mame/sessions \
     -video none -sound sdl -nothrottle -nowindow -skip_gameinfo \
     -wavwrite "$OUT" -seconds_to_run "$SECS" \
     -autoboot_script tools/labs/mame/sfxgrab.lua
