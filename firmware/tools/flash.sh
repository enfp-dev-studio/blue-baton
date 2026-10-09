#!/bin/sh
# 펌웨어를 빌드하고 플래시한다. 브리지가 떠 있으면 포트를 잠시 놓게 하고, 끝나면 다시 잡게 한다.
# 브리지에 붙은 설정 화면과 모니터는 끊기지 않고 자동으로 다시 붙는다.
#   firmware/tools/flash.sh            (빌드 + 플래시)
set -e
cd "$(dirname "$0")/.."
BRIDGE="${KM_BRIDGE:-http://127.0.0.1:8766}"
PORT="${KM_PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "보드 포트를 찾지 못했다 (/dev/cu.usbmodem*)"; exit 1; }
. "$HOME/esp/esp-idf/export.sh" >/dev/null 2>&1
LOG="${TMPDIR:-/tmp}/km_flash_build.log"
if ! idf.py build >"$LOG" 2>&1; then
  echo "빌드 실패 (전체 로그: $LOG)"
  grep -E "error|Error" "$LOG" | head -20
  exit 1
fi
released=0
if curl -sf -X POST "$BRIDGE/release" >/dev/null 2>&1; then
  released=1
  sleep 0.5
fi
status=0
idf.py -p "$PORT" flash >"$LOG" 2>&1 || status=$?
if [ "$released" = 1 ]; then
  curl -sf -X POST "$BRIDGE/acquire" >/dev/null || true
fi
if [ "$status" = 0 ]; then echo "플래시 완료 ($PORT)"; else echo "플래시 실패 (exit $status, 로그: $LOG)"; tail -5 "$LOG"; fi
exit "$status"
