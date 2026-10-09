#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "router.h"

/*
 * 출력 쪽: 보드가 호스트(Mac, Windows) 3대에 HOGP HID 장치(키보드 + 마우스)로 보인다.
 * 설계: devlog/proposals/2026-10-09-host-side-design.md (D-037~D-040)
 *
 * - 호스트 3대와 연결을 동시에 유지하고 활성 호스트에만 리포트를 보낸다.
 * - 호스트 페어링은 "호스트 추가 창"이 열린 동안만 받는다. 보드는 키보드(KeyboardOnly)로 알리고,
 *   호스트 화면의 6자리는 라우터에 붙은 키보드(또는 GUI)로 입력한다.
 * - 모든 함수는 NimBLE 호스트 태스크에서 불린다 (ble_pair.c가 요청을 넘겨준다).
 */

typedef void (*host_event_fn)(const char *json);

/* GATT 서비스를 등록한다. nimble_port_init 뒤, 호스트 태스크를 시작하기 전에 부른다 */
esp_err_t host_init(host_event_fn emit);

void host_on_sync(void);
void host_on_reset(void);

/* 라우터 출력을 활성 호스트로 보낸다. switched_from이 0 이상이면 그 호스트에 먼저 모두 뗌을 보낸다 */
void host_send(const router_out_t *out, int switched_from);

/* 패스키 받기 모드에서 라우터가 낸 진행(pk_event)을 처리한다 */
void host_passkey_event(const router_out_t *out);

/* 호스트 추가 창을 열거나 닫는다. 열려 있는 동안 새 호스트의 연결과 페어링을 받는다 (최대 180초) */
esp_err_t host_pair_window(bool open);
bool host_window_open(void);

/* GUI가 입력한 패스키 */
esp_err_t host_passkey(uint32_t code);

/* 호스트 하나를 지운다 (번호 0부터) */
esp_err_t host_remove(int index);

/* [{host, addr, name, connected, state, active}] */
size_t host_list_json(char *buf, size_t len);

/* 본딩 삭제(ble_gap_unpair)는 광고 중이면 EBUSY다. 지우는 동안 광고를 멈췄다가 되살린다 */
void host_adv_hold(bool hold);

/* 연결 칸 계산용: 지금 호스트 연결 수 */
int host_link_count(void);

/* 활성 호스트가 지금 입력을 받을 수 있는가 */
bool host_online(int index);
