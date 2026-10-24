#!/bin/sh
# BROWSER hook for `pebble emu-app-config`: opens the planner in headless Chrome and runs the
# steps in $PLANNER_STEPS (see tools/cdp.js), screenshots into $PLANNER_OUT. The tool waits
# for this command to exit, so the driver runs in the background.
cd "$(dirname "$0")/.." || exit 1
OUT="${PLANNER_OUT:-.}"
nohup node tools/cdp.js "$1" "${PLANNER_STEPS:?set PLANNER_STEPS}" "$OUT" > "$OUT/cdp.log" 2>&1 &
exit 0
