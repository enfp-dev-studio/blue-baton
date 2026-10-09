#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * 입력 장치(BLE HID, 보드가 Central) 관리.
 *
 * 규칙
 * - 키와 identity 주소의 원천은 NimBLE 본딩 저장소 하나다 (NVS에 영구 저장).
 * - BLE 상태는 NimBLE 호스트 태스크 하나가 소유한다. 아래 함수는 다른 태스크에서 불러도 되며,
 *   요청을 호스트 태스크로 넘겨 처리가 끝날 때까지 기다린다. 호스트 태스크 안에서는 부르지 않는다.
 * - 라디오(스캔, 연결 시도)와 보안 절차는 한 번에 하나씩 상태 기계로 처리한다.
 * - 본딩된 장치는 accept list로 자동 재연결한다 (HOGP 5.2.2 권장 방식).
 */

/* 보드가 밖으로 보내는 이벤트 한 개(JSON 문자열)를 받는 함수 */
typedef void (*ble_pair_event_fn)(const char *json);

/* NimBLE을 켜고, 이벤트를 event로 보낸다 */
esp_err_t ble_pair_init(ble_pair_event_fn event);

/* 새 장치 스캔을 켜거나 끈다. 켜 두면 60초 뒤 스스로 멈춘다. 스캔 중에는 자동 재연결을 쉰다 */
esp_err_t ble_pair_scan(bool on);

/* 찾은 장치 목록 (JSON 배열). all이 false면 HID 장치만 */
size_t ble_pair_list_json(char *buf, size_t len, bool all);

/* 연결된 장치 목록 (JSON 배열) */
size_t ble_pair_peers_json(char *buf, size_t len);

/* 페어링된 장치 목록 (JSON 배열, NimBLE 본딩 기준) */
size_t ble_pair_paired_json(char *buf, size_t len);

/* 페어링 하나를 지운다 (본딩 키, 해석 리스트, 메타데이터. 연결 중이면 끊는다) */
esp_err_t ble_pair_paired_remove(const char *addr);

/* 페어링을 모두 지운다 */
esp_err_t ble_pair_paired_clear(void);

/* addr("AA:BB:CC:DD:EE:FF")에 연결한다. 찾은 장치나 본딩된 장치여야 한다. 처음이면 페어링한다 */
esp_err_t ble_pair_connect(const char *addr);

/* 연결된 장치를 끊는다. 본딩은 남기고, 사용자가 다시 연결할 때까지 자동 재연결하지 않는다 */
esp_err_t ble_pair_disconnect(const char *addr);

/* 숫자 비교(NUMCMP) 요청에 수락하거나 거부한다 */
esp_err_t ble_pair_accept(bool accept);

/* 패스키 입력(INPUT) 요청에 6자리 코드를 보낸다 */
esp_err_t ble_pair_passkey(uint32_t code);

/* 스캔과 사용자 연결 시도를 멈추고, 진행 중인 페어링 입력을 거절한다. 이미 연결된 장치는 그대로 둔다 */
void ble_pair_cancel(void);

void ble_pair_status(bool *scanning, bool *connected, bool *hid_ready,
                     char *peer, size_t peer_len);

/* 라디오 상태: "idle", "scan", "connect", "auto", "cancel" */
const char *ble_pair_radio(void);

/* NimBLE 저장소에 있는 본딩 수. 실패하면 -1 */
int ble_pair_bond_count(void);

/* 호스트 전환 키 조합 "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3" (호스트 순서). 빈 문자열이면 기본값. 형식이 틀리면 ESP_ERR_INVALID_ARG */
esp_err_t ble_pair_set_combos(const char *text);

/* 활성 호스트를 직접 고른다 (0부터). 이전 호스트에는 모두 뗌을 보낸다 */
esp_err_t ble_pair_select_host(int host);

/* 활성 호스트 (0부터) */
int ble_pair_active_host(void);

/* ---- 호스트 (출력 쪽, host.c) ---- */

/* 호스트 추가 창 (최대 180초). 열린 동안 새 호스트가 페어링할 수 있다 */
esp_err_t ble_pair_host_pair(bool open);

/* 페어링된 호스트 목록 (JSON 배열) */
size_t ble_pair_host_list_json(char *buf, size_t len);

/* 호스트 하나를 지운다 (번호 0부터) */
esp_err_t ble_pair_host_remove(int index);

/* 호스트 화면의 패스키를 GUI에서 입력 */
esp_err_t ble_pair_host_passkey(uint32_t code);
