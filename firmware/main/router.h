#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hid_input.h"

/*
 * 입력 라우터: 입력 장치들의 공통 형태(hid_input_t)를 합쳐 "지금 활성 호스트로 보낼 상태"를 만들고,
 * 전환 키 조합을 가로챈다. 출력(호스트 쪽 BLE)과 무관하게 동작하므로 보드 없이 시험한다.
 *
 * 규칙 (일반 KVM 동작과 같다)
 * - 조합의 마지막 키가 눌리는 순간, 잡고 있는 수정 키 묶음(Ctrl, Shift, Alt, GUI)이 조합과 정확히 같고
 *   다른 일반 키가 없으면 전환한다. 왼쪽/오른쪽은 가리지 않는다.
 * - 그 순간 눌려 있던 키는 각각 뗄 때까지 어느 호스트에도 보내지 않는다 (조합이 새지 않고, 새 호스트에
 *   수정 키가 눌린 채로 넘어가지 않는다).
 * - 전환 때 이전 호스트에는 "모두 뗌" 상태를 보낸다 (키가 눌린 채로 남지 않게).
 * - 입력 장치가 끊기면 그 장치의 키와 버튼은 뗀 것으로 본다.
 * - 조합에 쓰이는 수정 키는 눌린 순간 보내지 않고 다음 사건에서 정한다 (보류). 조합이 완성되면 아무것도
 *   보내지 않고 전환한다. 다른 키, 마우스, 미디어 키가 오거나 보류한 수정 키를 떼면, 보류한 수정 키를 먼저
 *   보내고(pre_keys) 그 사건을 보낸다. 그래서 Ctrl+C는 그대로 되고, 수정 키 탭은 누름과 뗌으로 나간다.
 *   이전 호스트가 "Ctrl+Alt 눌렀다 뗌"을 보지 않게 하기 위해서다 (host-side-design 3.4).
 * - 패스키 받기 모드(호스트 페어링): 키 입력은 어느 호스트에도 보내지 않고 숫자를 모은다.
 */

#define ROUTER_HOST_MAX 3
#define ROUTER_INPUT_MAX 2
#define ROUTER_REPORT_MAX 16     /* 장치 하나의 입력 리포트(특성)별 상태. HID 서비스가 여럿이면 ID가 겹칠 수 있어 특성 번호로 구분한다 */

#define COMBO_CTRL 0x01
#define COMBO_SHIFT 0x02
#define COMBO_ALT 0x04
#define COMBO_GUI 0x08

typedef struct {
    bool valid;
    uint8_t mods;   /* COMBO_* 묶음 */
    uint8_t key;    /* Keyboard 페이지 Usage (수정 키가 아닌 것) */
} combo_t;

/* "ctrl+alt+1", "cmd+shift+f1", "ctrl+alt+0x3a" 형식. 대소문자 무관. 실패하면 false */
bool combo_parse(const char *text, combo_t *out);

/* 호스트별 조합 목록 "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3". 빈 항목은 그 호스트 조합 없음. 실패하면 false */
bool combo_parse_list(const char *text, combo_t out[ROUTER_HOST_MAX]);

/* 활성 호스트로 보낼 상태 */
typedef struct {
    uint8_t keys[32];                    /* 눌린 키 (수정 키 포함, 막힌 키 제외) */
    uint8_t buttons;
    int32_t dx, dy, wheel, pan;          /* 이번 입력의 이동 (누적 아님) */
    uint16_t consumer[HID_CONSUMER_MAX];
    uint8_t consumer_count;
    bool keys_changed;
    bool buttons_changed;
    bool consumer_changed;
    bool motion;
    /* 보류했던 수정 키를 풀 때: keys보다 먼저 이 키 상태를 키보드 리포트로 보낸다 */
    bool pre_valid;
    uint8_t pre_keys[32];
    /* 전환 때 새 활성 호스트의 LED 값 (입력 키보드에 써 준다) */
    bool led_changed;
    uint8_t led;
    /* 패스키 받기 모드의 진행 */
    uint8_t pk_event;   /* ROUTER_PK_* */
    uint8_t pk_len;     /* 지금까지 친 숫자 수 */
    uint32_t pk_code;   /* ROUTER_PK_DONE일 때 6자리 값 */
} router_out_t;

#define ROUTER_PK_NONE 0
#define ROUTER_PK_PROGRESS 1   /* 숫자를 치거나 지웠다 */
#define ROUTER_PK_DONE 2       /* Enter: 6자리 완성 */
#define ROUTER_PK_CANCEL 3     /* Esc */

void router_init(void);
void router_set_combos(const combo_t combos[ROUTER_HOST_MAX]);
int router_active_host(void);           /* 0부터 */
void router_set_active_host(int host);

/*
 * 입력 장치 slot의 리포트 하나를 반영한다. report_key는 장치 안에서 리포트를 구분하는 번호다
 * (펌웨어는 Report 특성 번호를 쓴다. HID 서비스가 여럿이면 Report ID가 겹칠 수 있다).
 * out: 활성 호스트로 보낼 상태. switched_from: 전환이 일어났으면 이전 호스트 번호, 아니면 -1.
 * 전환이 일어나면 이전 호스트에는 router_release_report()로 만든 "모두 뗌"을 보내야 한다.
 */
void router_input(uint8_t slot, uint8_t report_key, const hid_input_t *in, router_out_t *out, int *switched_from);

/* 입력 장치가 끊겼다. 그 장치가 누르던 것을 뗀 상태를 out에 채운다 */
void router_device_gone(uint8_t slot, router_out_t *out);

/* 모두 뗀 상태 (전환 때 이전 호스트로) */
void router_release_report(router_out_t *out);

/* 패스키 받기 모드를 시작/끝낸다. 시작하면 활성 호스트에 모두 뗌을 보내야 한다 (router_release_report) */
void router_passkey_begin(void);
void router_passkey_end(void);
bool router_passkey_active(void);

/* 호스트가 쓴 LED 출력 값 (Num, Caps, Scroll, Compose, Kana). 전환 때 router_out_t.led로 나온다 */
void router_set_host_led(int host, uint8_t led);
uint8_t router_host_led(int host);
