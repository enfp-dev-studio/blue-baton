#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#define CFG_PIN_MAX 8
#define CFG_WIFI_SSID_MAX 32
#define CFG_WIFI_PASS_MAX 64
#define CFG_AP_PASS_MAX 63
/* 보드 Wi-Fi 기본 비밀번호. 처음 접속할 때 쓰고, 웹 화면에서 바꿀 수 있다 */
#define CFG_DEFAULT_AP_PASS "kmrouter1"

/* 설정 저장소(NVS, 네임스페이스 km_cfg)를 연다. nvs_flash_init() 뒤에 호출한다 */
esp_err_t cfg_init(void);

bool cfg_pin_is_set(void);
bool cfg_pin_check(const char *pin);
esp_err_t cfg_pin_set(const char *pin);

/* 집 Wi-Fi 정보. 저장된 값이 있으면 true */
bool cfg_wifi_get(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t cfg_wifi_set(const char *ssid, const char *pass);
esp_err_t cfg_wifi_clear(void);

/* 저장된 보드 Wi-Fi 비밀번호. 없으면 기본값 */
void cfg_ap_pass_get(char *pass, size_t len);
esp_err_t cfg_ap_pass_set(const char *pass);

/* 호스트 전환 키 조합 목록 (예: "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3"). 저장된 값이 있으면 true */
#define CFG_COMBO_MAX 95
bool cfg_combo_get(char *combo, size_t len);
esp_err_t cfg_combo_set(const char *combo);

/*
 * 페어링된 기기의 부가 정보(메타데이터). 키와 주소의 원천은 NimBLE 본딩 저장소이고,
 * 여기에는 본딩 저장소에 없는 정보(이름, 종류, 마지막 연결 순서)만 둔다.
 * 키는 identity 주소(type 0 public, 1 random + 6바이트)다. 본딩 수만큼 둘 수 있다.
 */
#define CFG_PAIRED_MAX CONFIG_BT_NIMBLE_MAX_BONDS
typedef struct {
    uint8_t type;       /* identity 주소 종류 (0 public, 1 random) */
    uint8_t val[6];     /* NimBLE 주소 바이트 (뒤에서부터 저장) */
    char name[24];
    uint8_t kinds;      /* HID_KIND_* 비트 (Report Map에서 판정) */
    uint32_t last_seq;  /* 마지막으로 연결된 순서. 클수록 최근 */
    uint8_t role;       /* CFG_ROLE_* : 입력 장치인지 호스트인지 (NimBLE 저장소는 역할을 모른다) */
    uint8_t host_index; /* 호스트면 번호 1~3, 아니면 0 */
} cfg_paired_t;

#define CFG_ROLE_UNKNOWN 0
#define CFG_ROLE_INPUT 1
#define CFG_ROLE_HOST 2

int cfg_paired_list(cfg_paired_t *out, int max);     /* 기록 개수를 돌려준다 */
/* 같은 주소(type + val)면 갱신, 아니면 추가. 내용이 같으면 쓰지 않는다 */
esp_err_t cfg_paired_put(const cfg_paired_t *rec);
esp_err_t cfg_paired_remove(uint8_t type, const uint8_t val[6]);
esp_err_t cfg_paired_clear(void);
