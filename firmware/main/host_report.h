/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Reports sent to hosts (output side). One fixed Report Map, plus the rules that turn router state (router_out_t)
 * into that Report Map's report bytes.
 * It does not depend on NimBLE, so it is tested without the board.
 */

#define HOST_RID_KEYBOARD 1   /* Input 8 bytes, output (LED) 1 byte */
#define HOST_RID_MOUSE 2      /* 7 bytes: buttons 8, X 16, Y 16, wheel 8, horizontal wheel 8 */
#define HOST_RID_CONSUMER 3   /* 4 bytes: 2 slots of 16-bit Usage */
#define HOST_RID_SYSTEM 4     /* 1 byte: power off, sleep, wake */
#define HOST_RID_PEN 5        /* 11 bytes: tip, barrel, eraser, invert, in range; X, Y; tip pressure; X, Y tilt */

#define HOST_KB_LEN 8
#define HOST_MOUSE_LEN 7
#define HOST_CONSUMER_LEN 4
#define HOST_SYSTEM_LEN 1
#define HOST_BOOT_MOUSE_LEN 3
#define HOST_PEN_LEN 11
#define HOST_PEN_XY_MAX 32767
#define HOST_PEN_PRESSURE_MAX 4095
#define HOST_PEN_TILT_MAX 9000

extern const uint8_t HOST_REPORT_MAP[];
extern const size_t HOST_REPORT_MAP_LEN;

/* Mouse motion accumulator. Motion that does not fit in one report carries over to the next */
typedef struct {
    int32_t dx, dy, wheel, pan;
} host_motion_t;

/*
 * Keyboard report, 8 bytes: modifier (0xE0-0xE7) bits, reserved 0, 6 key slots.
 * Keys use only 0x04-0xDF. With 7 or more, all 6 slots are ErrorRollOver (0x01) and modifiers stay as is (HID 1.11 Appendix C).
 */
void host_kb_report(const uint8_t keys[32], uint8_t out[HOST_KB_LEN]);

/* Consumer control report, 4 bytes: the first 2 Usages */
void host_consumer_report(const uint16_t *usages, uint8_t count, uint8_t out[HOST_CONSUMER_LEN]);

/*
 * A pen (stylus) state: absolute position, pressure and tilt (HID Usage Tables, Digitizers page). As a pen reports
 * (Microsoft, "Required HID top-level collections", pen): in_range whenever the tip touches, pressure only while it
 * touches, and the report that lifts the tip or leaves range repeats the last position.
 */
typedef struct {
    bool tip, barrel, eraser, invert, in_range;
    uint16_t x, y;            /* 0..HOST_PEN_XY_MAX across the digitizer */
    uint16_t pressure;        /* 0..HOST_PEN_PRESSURE_MAX */
    int16_t tilt_x, tilt_y;   /* 0.01 degree, -HOST_PEN_TILT_MAX..HOST_PEN_TILT_MAX */
} host_pen_t;

/* Pen report, 11 bytes */
void host_pen_report(const host_pen_t *p, uint8_t out[HOST_PEN_LEN]);

/* Accumulate motion */
void host_motion_add(host_motion_t *m, int32_t dx, int32_t dy, int32_t wheel, int32_t pan);

/* Whether the accumulator is empty */
bool host_motion_empty(const host_motion_t *m);

/* Build a 7-byte mouse report and subtract what it carries from the accumulator. X/Y clamp to ±32767, wheel and horizontal wheel to ±127 */
void host_mouse_report(host_motion_t *m, uint8_t buttons, uint8_t out[HOST_MOUSE_LEN]);

/* Boot Mouse report, 3 bytes (low 3 button bits, X, Y at ±127). For hosts in Boot mode */
void host_boot_mouse_report(host_motion_t *m, uint8_t buttons, uint8_t out[HOST_BOOT_MOUSE_LEN]);

/*
 * "Last sent values" for one host connection. Used to avoid resending the same value (HIDS, Apple),
 * and to send a release on switch only for reports that are nonzero.
 */
typedef struct {
    uint8_t kb[HOST_KB_LEN];
    uint8_t buttons;
    uint8_t consumer[HOST_CONSUMER_LEN];
    uint8_t system;
    uint8_t pen[HOST_PEN_LEN];
} host_sent_t;
