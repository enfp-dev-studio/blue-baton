#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Parse an input device's Report Map into a layout, and decode input reports into a common form.
 * Uses only HID 1.11 Report Descriptor rules and the HID Usage Tables, with no per-device assumptions.
 * Same rules as the UI-side parser (ui/js/hid/report-map.js).
 */

#define HID_PAGE_DESKTOP 0x01
#define HID_PAGE_KEYBOARD 0x07
#define HID_PAGE_BUTTON 0x09
#define HID_PAGE_CONSUMER 0x0C

#define HID_FIELD_MAX 48       /* Input items per Report Map (real-hardware keyboards have around 20) */
#define HID_REPORT_MAX 16      /* Input report IDs per Report Map */
#define HID_CONSUMER_MAX 4     /* Media keys pressed at once in one report */

/* One Input item (Report Count fields of the same size) */
typedef struct {
    uint8_t report_id;
    uint16_t bit_offset;    /* Start bit within the report (excluding the report ID byte) */
    uint8_t bit_size;       /* Bits per field */
    uint16_t count;         /* Number of fields (an NKRO bitmap can have hundreds) */
    uint16_t page;
    uint16_t usage_min;     /* Start of the Usage range. Variable: field n = usage_min + n; Array: value v = usage_min + (v - logical_min) */
    int32_t logical_min;
    bool array;             /* Data/Array bit of the Input Main item */
    bool relative;
    bool has_usage_range;
    uint16_t usages[6];     /* For Variable with Usages listed one by one (the last repeats if too few) */
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
    bool truncated;         /* Ran out of slots and dropped some */
} hid_layout_t;

/* Result of decoding one report */
typedef struct {
    uint8_t keys[32];                     /* Pressed bitmap of Keyboard page Usages 0-255 (including modifiers 0xE0-0xE7) */
    bool has_keys;                        /* This report has keyboard fields */
    uint8_t buttons;                      /* Mouse buttons 1-8 bitmap (bit0 = button 1) */
    bool has_buttons;
    int32_t dx, dy, wheel, pan;           /* Relative motion */
    bool has_motion;
    uint16_t consumer[HID_CONSUMER_MAX];  /* Usages of pressed media keys */
    uint8_t consumer_count;
    bool has_consumer;
} hid_input_t;

/* Build a layout from a Report Map. On a malformed item, keep what was read up to that point */
void hid_layout_parse(const uint8_t *map, size_t len, hid_layout_t *out);

/* Length (bits) of a report ID. 0 if absent */
uint16_t hid_layout_report_bits(const hid_layout_t *layout, uint8_t report_id);

/*
 * Decode one input report. report_id comes from the Report Reference.
 * If the notification data is prefixed with the report ID byte (one byte longer and the first byte is the ID),
 * strip it before reading.
 * false if the report is not in the layout.
 */
bool hid_decode(const hid_layout_t *layout, uint8_t report_id, const uint8_t *data, size_t len, hid_input_t *out);

static inline bool hid_key_down(const uint8_t keys[32], uint8_t usage)
{
    return (keys[usage >> 3] >> (usage & 7)) & 1;
}
