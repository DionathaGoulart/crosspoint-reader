#!/bin/sh
# Drive the host simulator with a scripted input run and collect screenshots.
#
#   sim/run.sh '0:WAIT:Entering activity: Home;800:SHOT:sim/out/home.bmp;1500:QUIT'
#
# Set SIM_WINDOW=1 to watch it in a real SDL window instead of running headless.
# Everything it writes lands in sim/out/, which is ignored by git.
#
# See sim/README.md for the script syntax and the traps worth knowing.
set -eu
cd "$(dirname "$0")/.."

# SIM_ENV picks the PlatformIO environment; the X4 Classic one is what this
# fork targets, and the plain `simulator` env has no PSRAM so it cannot draw
# the cover-grid homes.
SIM_ENV="${SIM_ENV:-simulator_x4_classic}"
PROGRAM=".pio/build/$SIM_ENV/program"
if [ ! -x "$PROGRAM" ]; then
  echo "simulator not built; run: pio run -e $SIM_ENV" >&2
  exit 1
fi
if [ $# -lt 1 ]; then
  echo "usage: sim/run.sh '<CROSSPOINT_SIM_INPUT_SCRIPT>'" >&2
  exit 2
fi

mkdir -p sim/out fs_

# Headless by default: a real window needs a desktop session, and the SDL dummy
# driver is what the simulator's own tooling uses for scripted runs.
if [ "${SIM_WINDOW:-0}" = "1" ]; then
  unset SDL_VIDEODRIVER
else
  SDL_VIDEODRIVER=dummy
  export SDL_VIDEODRIVER
fi

CROSSPOINT_SIM_SD="${CROSSPOINT_SIM_SD:-./fs_}"
CROSSPOINT_SIM_SCREEN_TEXT="${CROSSPOINT_SIM_SCREEN_TEXT:-1}"
CROSSPOINT_SIM_INPUT_SCRIPT="$1"
export CROSSPOINT_SIM_SD CROSSPOINT_SIM_SCREEN_TEXT CROSSPOINT_SIM_INPUT_SCRIPT

# The program's own status, not a pipeline's. Piping into grep would report
# grep's status instead, so a mid-script crash would look like a clean run that
# simply printed fewer screens -- and these runs are used as evidence.
# The status file is how the program's own code escapes the pipeline: `sh` has
# no pipefail, so without it the status would be grep's.
status_file="sim/out/.exit-status"
rm -f "$status_file"
set +e
{ "$PROGRAM" 2>&1; echo $? > "$status_file"; } | tee sim/out/last-run.log |
  grep -E '^\[SIM\] (SCREEN|WAIT|Saved)'
set -e
status=$(cat "$status_file" 2>/dev/null || echo 1)
rm -f "$status_file"

# BMP is what the simulator writes; PNG is what most viewers and diff tools want.
if command -v sips >/dev/null 2>&1; then
  for bmp in sim/out/*.bmp; do
    [ -e "$bmp" ] || continue
    sips -s format png "$bmp" --out "${bmp%.bmp}.png" >/dev/null 2>&1 || true
  done
fi

if [ "$status" -ne 0 ]; then
  echo "simulator exited $status; full output in sim/out/last-run.log" >&2
fi
exit "$status"
