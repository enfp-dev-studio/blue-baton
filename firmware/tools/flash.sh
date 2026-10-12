#!/bin/sh
# Build and flash the developer firmware (diagnostics on). If the bridge is running, it is asked to release the port for a moment and re-acquire it afterwards.
# Settings UI and monitor clients attached to the bridge are not dropped; they reattach automatically.
#   firmware/tools/flash.sh            (build + flash)
set -e
cd "$(dirname "$0")/.."
BRIDGE="${KM_BRIDGE:-http://127.0.0.1:8766}"
PORT="${KM_PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "Board port not found (/dev/cu.usbmodem*)"; exit 1; }
. "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1
LOG="${TMPDIR:-/tmp}/km_flash_build.log"
# Developer build: diagnostics on (sdkconfig.dev), in its own build folder so a release build (idf.py build) stays separate
IDF="idf.py -B build-dev -DSDKCONFIG=build-dev/sdkconfig -DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.dev"
if ! $IDF build >"$LOG" 2>&1; then
  echo "Build failed (full log: $LOG)"
  grep -E "error|Error" "$LOG" | head -20
  exit 1
fi
released=0
if curl -sf -X POST "$BRIDGE/release" >/dev/null 2>&1; then
  released=1
  sleep 0.5
fi
status=0
$IDF -p "$PORT" flash >"$LOG" 2>&1 || status=$?
if [ "$released" = 1 ]; then
  curl -sf -X POST "$BRIDGE/acquire" >/dev/null || true
fi
if [ "$status" = 0 ]; then echo "Flash done ($PORT)"; else echo "Flash failed (exit $status, log: $LOG)"; tail -5 "$LOG"; fi
exit "$status"
