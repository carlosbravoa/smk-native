#!/bin/sh
# Run one of the USER'S OWN savestates headlessly with a script attached.
#
#   tools/labs/mame/state.sh <slot> <script.lua> [seconds]
#
# No -seconds_to_run: loading a state does a SOFT RESET, and MAME
# ends the run on it.  The script exits the machine itself.
#
# Their states live in their own MAME config ($HOME/.mame/sta), not in
# tools/labs/mame/sessions - a state is a position they set up by hand,
# where a recording is a whole run.  Never opens a window.
set -e
cd "$(dirname "$0")/../../.."
SDL_VIDEODRIVER=dummy
SDL_AUDIODRIVER=dummy
export SDL_VIDEODRIVER SDL_AUDIODRIVER
# The slot goes to the SCRIPT, not to MAME: -state on the command
# line cancels -autoboot_script, so the script loads it itself.
SMK_STATE="$1"; SCRIPT="$2"; SMK_SECS="${3:-30}"; export SMK_STATE SMK_SECS
exec mame snes -cart rom/smk_usa.sfc \
     -state_directory "${SMK_STATE_DIR:-$HOME/.mame/sta}" \
     -video none -sound none -nothrottle -nowindow -skip_gameinfo \
     -autoboot_script "$SCRIPT"
