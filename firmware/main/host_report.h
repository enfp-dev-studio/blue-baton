#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 호스트로 보내는 리포트 (출력 쪽). 고정 Report Map 하나와, 라우터 상태(router_out_t)를
 * 그 Report Map의 리포트 바이트로 바꾸는 규칙이다 (proposals/2026-10-09-host-side-design.md 4.3).
 * NimBLE에 의존하지 않으므로 보드 없이 시험한다.
 */

#define HOST_RID_KEYBOARD 1   /* 입력 8바이트, 출력(LED) 1바이트 */
#define HOST_RID_MOUSE 2      /* 7바이트: 버튼 8, X 16, Y 16, 휠 8, 가로 휠 8 */
#define HOST_RID_CONSUMER 3   /* 4바이트: 16비트 Usage 2칸 */
#define HOST_RID_SYSTEM 4     /* 1바이트: 전원 끔, 잠자기, 깨우기 */

#define HOST_KB_LEN 8
#define HOST_MOUSE_LEN 7
#define HOST_CONSUMER_LEN 4
#define HOST_SYSTEM_LEN 1
#define HOST_BOOT_MOUSE_LEN 3

extern const uint8_t HOST_REPORT_MAP[];
extern const size_t HOST_REPORT_MAP_LEN;

/* 마우스 이동 누적기. 리포트 한 칸에 다 못 담은 이동을 다음 리포트로 넘긴다 */
typedef struct {
    int32_t dx, dy, wheel, pan;
} host_motion_t;

/*
 * 키보드 리포트 8바이트: 수정 키(0xE0~0xE7) 비트, 예약 0, 일반 키 6칸.
 * 일반 키는 0x04~0xDF만 쓴다. 7개 이상이면 6칸 모두 ErrorRollOver(0x01)이고 수정 키는 그대로 (HID 1.11 부록 C).
 */
void host_kb_report(const uint8_t keys[32], uint8_t out[HOST_KB_LEN]);

/* 소비자 제어 리포트 4바이트: 앞의 Usage 2개 */
void host_consumer_report(const uint16_t *usages, uint8_t count, uint8_t out[HOST_CONSUMER_LEN]);

/* 이동을 누적한다 */
void host_motion_add(host_motion_t *m, int32_t dx, int32_t dy, int32_t wheel, int32_t pan);

/* 누적기가 비었는지 */
bool host_motion_empty(const host_motion_t *m);

/* 마우스 리포트 7바이트를 만들고 담은 만큼 누적기에서 뺀다. X/Y는 ±32767, 휠과 가로 휠은 ±127로 자른다 */
void host_mouse_report(host_motion_t *m, uint8_t buttons, uint8_t out[HOST_MOUSE_LEN]);

/* Boot Mouse 리포트 3바이트 (버튼 하위 3비트, X, Y는 ±127). Boot 모드 호스트용 */
void host_boot_mouse_report(host_motion_t *m, uint8_t buttons, uint8_t out[HOST_BOOT_MOUSE_LEN]);

/*
 * 호스트 연결 하나의 "마지막으로 보낸 값". 같은 값을 되풀이해 보내지 않고(HIDS, Apple),
 * 전환 때 0이 아닌 리포트에만 뗌을 보내는 데 쓴다.
 */
typedef struct {
    uint8_t kb[HOST_KB_LEN];
    uint8_t buttons;
    uint8_t consumer[HOST_CONSUMER_LEN];
    uint8_t system;
} host_sent_t;

/* 새 값이 마지막으로 보낸 값과 다르면 last를 갱신하고 true (보내야 함) */
bool host_sent_update(uint8_t *last, const uint8_t *now, size_t len);

/* 전환 때 이전 호스트로 보낼 뗌: 0이 아닌 리포트만 표시한다 */
typedef struct {
    bool kb, mouse, consumer, system;
} host_release_t;

host_release_t host_release_needed(const host_sent_t *sent);
