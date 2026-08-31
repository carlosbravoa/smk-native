#!/bin/sh
# grab.sh with a chosen lua script: <session> <out.wav> <seconds> <script>
set -e
cd "$(dirname "$0")/../../.."
NAME="$1"; OUT="$2"; SECS="${3:-60}"; SCRIPT="$4"
SDL_AUDIODRIVER=dummy exec mame snes -cart rom/smk_usa.sfc \
     -input_directory tools/labs/mame/sessions -playback "$NAME" \
     -state_directory tools/labs/mame/sessions \
     -video none -sound sdl -nothrottle -window -skip_gameinfo \
     -wavwrite "$OUT" -seconds_to_run "$SECS" -autoboot_script "$SCRIPT"
