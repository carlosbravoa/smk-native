#!/bin/sh
# Replay a recorded session headlessly with instrumentation attached.
#
#   tools/labs/mame/replay.sh <name> <script.lua> [seconds]
#
# The Lua script sees the same frames the player saw, so a watch can be
# put on anything and the run repeated as often as needed.
set -e
cd "$(dirname "$0")/../../.."
NAME="$1"; SCRIPT="$2"; SECS="${3:-120}"
exec mame snes -cart rom/smk_usa.sfc \
     -input_directory tools/labs/mame/sessions -playback "$NAME" \
     -state_directory tools/labs/mame/sessions \
     -video none -sound none -nothrottle -window -skip_gameinfo \
     -seconds_to_run "$SECS" -autoboot_script "$SCRIPT"
