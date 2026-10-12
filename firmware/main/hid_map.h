/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* HID device kinds (determined from the Report Map's top-level application collections) */
#define HID_KIND_KEYBOARD 0x01   /* Generic Desktop: Keyboard 0x06, Keypad 0x07 */
#define HID_KIND_MOUSE    0x02   /* Generic Desktop: Mouse 0x02, Pointer 0x01 */
#define HID_KIND_CONSUMER 0x04   /* Consumer: Consumer Control 0x01 (volume, playback, etc.) */
#define HID_KIND_SYSTEM   0x08   /* Generic Desktop: System Control 0x80 (power, sleep) */
#define HID_KIND_GAME     0x10   /* Generic Desktop: Joystick 0x04, Gamepad 0x05 */
#define HID_KIND_OTHER    0x80   /* Any other application collection (vendor-specific, etc.) */

/*
 * Read a Report Map (HID 1.11 6.2.2 Report Descriptor) and return the kind bits.
 * What a device can do is determined by the Usage of its top-level application collections.
 * On a malformed item, returns the result read up to that point.
 */
uint8_t hid_map_kinds(const uint8_t *map, size_t len);

/* Write the kind bits as a JSON array string. e.g. ["keyboard","consumer"] */
void hid_kinds_json(uint8_t kinds, char *out, size_t len);
