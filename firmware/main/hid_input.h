#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 입력 장치의 Report Map을 해석해 두고(레이아웃), 입력 리포트를 공통 형태로 푼다.
 * 기기별 가정 없이 HID 1.11 Report Descriptor 규칙과 HID Usage Tables만 쓴다 (D-026, D-030).
 * 화면 쪽 해석기(ui/js/hid/report-map.js)와 같은 규칙이다.
 */

#define HID_PAGE_DESKTOP 0x01
#define HID_PAGE_KEYBOARD 0x07
#define HID_PAGE_BUTTON 0x09
#define HID_PAGE_CONSUMER 0x0C

#define HID_FIELD_MAX 48       /* 한 Report Map의 Input 항목 수 (실기 키보드는 20개 안팎) */
#define HID_REPORT_MAX 16      /* 한 Report Map의 입력 리포트 ID 수 */
#define HID_CONSUMER_MAX 4     /* 한 리포트에서 동시에 눌린 미디어 키 */

/* Input 항목 하나 (Report Count개의 같은 크기 필드) */
typedef struct {
    uint8_t report_id;
    uint16_t bit_offset;    /* 리포트 안에서의 시작 비트 (리포트 ID 바이트 제외) */
    uint8_t bit_size;       /* 필드 하나의 비트 수 */
    uint16_t count;         /* 필드 수 (NKRO 비트맵은 수백 개일 수 있다) */
    uint16_t page;
    uint16_t usage_min;     /* Usage 범위의 시작. Variable은 필드 n = usage_min + n, Array는 값 v = usage_min + (v - logical_min) */
    int32_t logical_min;
    bool array;             /* Input Main 항목의 Data/Array 비트 */
    bool relative;
    bool has_usage_range;
    uint16_t usages[6];     /* Variable이고 Usage를 하나씩 나열한 경우 (모자라면 마지막이 반복) */
    uint8_t usage_count;
} hid_field_t;

typedef struct {
    uint8_t id;
    uint16_t bits;
} hid_report_info_t;

typedef struct {
    hid_field_t fields[HID_FIELD_MAX];
    uint8_t field_count;
    hid_report_info_t reports[HID_REPORT_MAX];
    uint8_t report_count;
    bool truncated;         /* 칸이 모자라 일부를 버렸다 */
} hid_layout_t;

/* 리포트 하나를 푼 결과 */
typedef struct {
    uint8_t keys[32];                     /* Keyboard 페이지 Usage 0~255의 눌림 비트맵 (수정 키 0xE0~0xE7 포함) */
    bool has_keys;                        /* 이 리포트에 키보드 필드가 있다 */
    uint8_t buttons;                      /* 마우스 버튼 1~8 비트맵 (bit0 = 버튼 1) */
    bool has_buttons;
    int32_t dx, dy, wheel, pan;           /* 상대 이동 */
    bool has_motion;
    uint16_t consumer[HID_CONSUMER_MAX];  /* 눌린 미디어 키 Usage */
    uint8_t consumer_count;
    bool has_consumer;
} hid_input_t;

/* Report Map을 레이아웃으로 만든다. 깨진 항목을 만나면 그때까지 읽은 것을 남긴다 */
void hid_layout_parse(const uint8_t *map, size_t len, hid_layout_t *out);

/* 리포트 ID의 길이(비트). 없으면 0 */
uint16_t hid_layout_report_bits(const hid_layout_t *layout, uint8_t report_id);

/*
 * 입력 리포트 하나를 푼다. report_id는 Report Reference에서 온 값이다.
 * 알림 데이터 앞에 리포트 ID 바이트가 붙어 온 경우(길이가 1바이트 더 길고 첫 바이트가 ID)는 떼고 읽는다.
 * 레이아웃에 없는 리포트면 false.
 */
bool hid_decode(const hid_layout_t *layout, uint8_t report_id, const uint8_t *data, size_t len, hid_input_t *out);

static inline bool hid_key_down(const uint8_t keys[32], uint8_t usage)
{
    return (keys[usage >> 3] >> (usage & 7)) & 1;
}
