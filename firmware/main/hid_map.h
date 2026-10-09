#pragma once

#include <stddef.h>
#include <stdint.h>

/* HID 장치 종류 (Report Map의 top-level application collection으로 판정) */
#define HID_KIND_KEYBOARD 0x01   /* Generic Desktop: Keyboard 0x06, Keypad 0x07 */
#define HID_KIND_MOUSE    0x02   /* Generic Desktop: Mouse 0x02, Pointer 0x01 */
#define HID_KIND_CONSUMER 0x04   /* Consumer: Consumer Control 0x01 (볼륨, 재생 등) */
#define HID_KIND_SYSTEM   0x08   /* Generic Desktop: System Control 0x80 (전원, 잠자기) */
#define HID_KIND_GAME     0x10   /* Generic Desktop: Joystick 0x04, Gamepad 0x05 */
#define HID_KIND_OTHER    0x80   /* 그 밖의 application collection (제조사 전용 등) */

/*
 * Report Map(HID 1.11 6.2.2 Report Descriptor)을 읽어 종류 비트를 돌려준다.
 * 장치가 어떤 기능을 가졌는지는 top-level application collection의 Usage로 정해진다.
 * 형식이 깨진 항목을 만나면 그때까지 읽은 결과를 돌려준다.
 */
uint8_t hid_map_kinds(const uint8_t *map, size_t len);

/* 종류 비트를 JSON 배열 문자열로 쓴다. 예: ["keyboard","consumer"] */
void hid_kinds_json(uint8_t kinds, char *out, size_t len);
