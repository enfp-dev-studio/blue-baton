#!/bin/sh
# 보드 없이 도는 시험 전체.
#  1) 펌웨어 C 코드 (Report Map 종류 판정): 주소 검사기(ASan, UBSan)를 켜서 범위 밖 읽기도 잡는다
#  2) 설정 화면 JS (Report Map 해석, 모의 보드와의 명령·이벤트 흐름): Node 내장 test
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
echo "== firmware C: json_mini (UTF-8 name trimming, JSON escape)"
cc -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined \
   -o "${OUT}_text" test_text.c ../main/json_mini.c
"${OUT}_text"
if command -v node >/dev/null 2>&1; then
  echo "== ui JS: report map, mock board flow"
  node --test ../ui/test/
else
  echo "== ui JS: skipped (node not found)"
fi
