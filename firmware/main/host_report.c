#include <string.h>

#include "host_report.h"

/*
 * Fixed 194-byte Report Map. Uses only standard items from HID 1.11 and HID Usage Tables 1.6.
 * Every report is 8 bytes or less, so it fits in the default ATT_MTU (20-byte notifications).
 */
const uint8_t HOST_REPORT_MAP[] = {
    /* ID1 keyboard: 8 modifier bits, 1 reserved byte, 6-slot key array (Usage 0-255, declaring the range Apple accepts) */
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,
    0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00,
    0x81, 0x00,
    /* ID1 keyboard output: 5 LED bits (Num, Caps, Scroll, Compose, Kana) + 3 padding bits */
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0xC0,
    /* ID2 mouse: buttons 8, X/Y 16-bit relative, wheel 8, horizontal wheel (AC Pan) 8 */
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x16, 0x01, 0x80, 0x26, 0xFF, 0x7F, 0x75, 0x10, 0x95, 0x02,
    0x81, 0x06,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x01, 0x81, 0x06,
    0xC0, 0xC0,
    /* ID3 consumer control: 2-slot array of 16-bit Usages (0-0x3FF) */
    0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x03, 0x15, 0x00, 0x26, 0xFF, 0x03,
    0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x02, 0x81, 0x00,
    0xC0,
    /* ID4 system control: 0x81 power off, 0x82 sleep, 0x83 wake + 5 padding bits */
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, 0x04, 0x19, 0x81, 0x29, 0x83,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x03, 0x81, 0x02, 0x95, 0x05, 0x81, 0x01,
    0xC0,
};
const size_t HOST_REPORT_MAP_LEN = sizeof(HOST_REPORT_MAP);

#define USAGE_FIRST_KEY 0x04
#define USAGE_LAST_KEY 0xDF       /* 0xE0-0xE7 are modifiers, 0xE8 and up are reserved */
#define USAGE_MOD_FIRST 0xE0
#define ERROR_ROLLOVER 0x01
#define KB_SLOTS 6

static bool key_down(const uint8_t keys[32], unsigned u)
{
    return (keys[u >> 3] >> (u & 7)) & 1;
}

void host_kb_report(const uint8_t keys[32], uint8_t out[HOST_KB_LEN])
{
    memset(out, 0, HOST_KB_LEN);
    for (unsigned u = USAGE_MOD_FIRST; u <= USAGE_MOD_FIRST + 7; u++) {
        if (key_down(keys, u)) {
            out[0] |= (uint8_t)(1u << (u - USAGE_MOD_FIRST));
        }
    }
    int n = 0;
    for (unsigned u = USAGE_FIRST_KEY; u <= USAGE_LAST_KEY; u++) {
        if (!key_down(keys, u)) {
            continue;
        }
        if (n == KB_SLOTS) {
            /* More keys pressed than slots: fill every slot with ErrorRollOver (modifiers stay as is) */
            memset(out + 2, ERROR_ROLLOVER, KB_SLOTS);
            return;
        }
        out[2 + n++] = (uint8_t)u;
    }
}

void host_consumer_report(const uint16_t *usages, uint8_t count, uint8_t out[HOST_CONSUMER_LEN])
{
    memset(out, 0, HOST_CONSUMER_LEN);
    for (int i = 0; i < count && i < 2; i++) {
        uint16_t u = usages[i] > 0x3FF ? 0 : usages[i];
        out[i * 2] = (uint8_t)(u & 0xFF);
        out[i * 2 + 1] = (uint8_t)(u >> 8);
    }
}

void host_motion_add(host_motion_t *m, int32_t dx, int32_t dy, int32_t wheel, int32_t pan)
{
    m->dx += dx;
    m->dy += dy;
    m->wheel += wheel;
    m->pan += pan;
}

bool host_motion_empty(const host_motion_t *m)
{
    return m->dx == 0 && m->dy == 0 && m->wheel == 0 && m->pan == 0;
}

static int32_t take(int32_t *acc, int32_t limit)
{
    int32_t v = *acc;
    if (v > limit) {
        v = limit;
    } else if (v < -limit) {
        v = -limit;
    }
    *acc -= v;
    return v;
}

void host_mouse_report(host_motion_t *m, uint8_t buttons, uint8_t out[HOST_MOUSE_LEN])
{
    int32_t x = take(&m->dx, 32767);
    int32_t y = take(&m->dy, 32767);
    int32_t w = take(&m->wheel, 127);
    int32_t p = take(&m->pan, 127);
    out[0] = buttons;
    out[1] = (uint8_t)(x & 0xFF);
    out[2] = (uint8_t)((x >> 8) & 0xFF);
    out[3] = (uint8_t)(y & 0xFF);
    out[4] = (uint8_t)((y >> 8) & 0xFF);
    out[5] = (uint8_t)(int8_t)w;
    out[6] = (uint8_t)(int8_t)p;
}

void host_boot_mouse_report(host_motion_t *m, uint8_t buttons, uint8_t out[HOST_BOOT_MOUSE_LEN])
{
    out[0] = buttons & 0x07;
    out[1] = (uint8_t)(int8_t)take(&m->dx, 127);
    out[2] = (uint8_t)(int8_t)take(&m->dy, 127);
    /* Boot Mouse has no wheel. Discard accumulated wheel (a Boot host ignores Report Protocol reports) */
    m->wheel = 0;
    m->pan = 0;
}

bool host_sent_update(uint8_t *last, const uint8_t *now, size_t len)
{
    if (memcmp(last, now, len) == 0) {
        return false;
    }
    memcpy(last, now, len);
    return true;
}

static bool nonzero(const uint8_t *p, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (p[i]) {
            return true;
        }
    }
    return false;
}

host_release_t host_release_needed(const host_sent_t *sent)
{
    host_release_t r = {
        .kb = nonzero(sent->kb, HOST_KB_LEN),
        .mouse = sent->buttons != 0,
        .consumer = nonzero(sent->consumer, HOST_CONSUMER_LEN),
        .system = sent->system != 0,
    };
    return r;
}
