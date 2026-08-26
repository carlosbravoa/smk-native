#!/bin/sh
# Play the real game in MAME and record everything, so the session can be
# replayed here deterministically (NOTES 123).
#
#   tools/labs/mame/play.sh [name]        default name: gv
#
# It records your inputs to tools/labs/mame/sessions/<name> and puts save
# states next to it.  While playing:
#
#   Shift+F7 then 1   save a state in slot 1  (park it JUST BEFORE the
#                     thing you want me to look at)
#   F7 then 1         load that state back
#   Esc               quit - the recording is written on exit
#
# Send me the whole sessions/ directory.
set -e
cd "$(dirname "$0")/../../.."
NAME="${1:-gv}"
DIR="tools/labs/mame/sessions"
mkdir -p "$DIR"
echo "recording to $DIR/$NAME  (Esc to finish)"
exec mame snes -cart rom/smk_usa.sfc \
     -input_directory "$DIR" -record "$NAME" \
     -state_directory "$DIR" \
     -window -skip_gameinfo -nomouse
