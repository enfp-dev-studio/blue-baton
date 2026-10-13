#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# All tests that run without a board.
#  1) Firmware C code (Report Map kind detection): built with sanitizers (ASan, UBSan) so out-of-bounds reads are caught too
#  2) Settings UI JS (Report Map parsing, command/event flow against the mock board): Node's built-in test runner
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/km_test_hid_map"
echo "== firmware C: hid_map"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
   -o "$OUT" test_hid_map.c ../main/hid_map.c
"$OUT"
echo "== firmware C: hid_input + router + host_report (input normalization, host switch, output reports)"
cc -std=gnu11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
   -o "${OUT}_router" test_router.c ../main/hid_input.c ../main/router.c ../main/host_report.c
"${OUT}_router"
echo "== firmware C: json_mini (UTF-8 name trimming and counting, JSON escape)"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
   -o "${OUT}_text" test_text.c ../main/json_mini.c
"${OUT}_text"
if command -v node >/dev/null 2>&1; then
  echo "== ui JS: report map, mock board flow, i18n tables"
  node --test ../../ui/test/
else
  echo "== ui JS: skipped (node not found)"
fi
