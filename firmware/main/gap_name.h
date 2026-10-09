#pragma once

#include <stdint.h>

/*
 * 상대의 GAP Device Name(0x2A00)을 끝까지 읽는다 (입력 장치와 호스트 모두).
 *
 * Read By Type 응답에는 값이 ATT_MTU-4바이트까지만 담긴다 (Core v5.4 Vol 3 Part F 3.4.4.2).
 * MTU를 바꾸기 전이면 19바이트에서 잘린다 ("Example’s Mac mini"가 "Example’s Mac min"이 됨).
 * 그래서 잘렸을 수 있으면 얻은 핸들로 Read Blob(긴 읽기)을 이어서 한다 (같은 절 3.4.4.5).
 * 받는 길이는 저장할 이름 길이(GAP_NAME_MAX)까지이고, 끝에서 잘린 UTF-8 글자는 버린다.
 * NimBLE 호스트 태스크에서만 부른다.
 */

#define GAP_NAME_MAX 23   /* cfg_paired_t.name에 들어가는 바이트 수 */

typedef void (*gap_name_fn)(uint16_t conn, const char *name);

/* 0이면 읽기를 시작했다. 다 읽으면 fn을 부른다 (실패하거나 이름이 비면 부르지 않는다) */
int gap_name_read(uint16_t conn, gap_name_fn fn);
