#include <string.h>

#include "hid_input.h"

/* Item types and tags (HID 1.11 6.2.2.4-6.2.2.8) */
#define TYPE_MAIN 0
#define TYPE_GLOBAL 1
#define TYPE_LOCAL 2
#define TAG_INPUT 0x8
#define TAG_OUTPUT 0x9
#define TAG_USAGE_PAGE 0x0
#define TAG_LOGICAL_MIN 0x1
#define TAG_REPORT_SIZE 0x7
#define TAG_REPORT_ID 0x8
#define TAG_REPORT_COUNT 0x9
#define TAG_PUSH 0xA
#define TAG_POP 0xB
#define TAG_USAGE 0x0
#define TAG_USAGE_MIN 0x1
#define INPUT_CONSTANT 0x01
#define INPUT_VARIABLE 0x02
#define INPUT_RELATIVE 0x04
#define LONG_ITEM 0xFE
#define STACK_MAX 4

/* Keyboard page 0x01-0x03 are error indicators (ErrorRollOver etc.), not keys (HID Usage Tables chapter 10) */
#define KEY_FIRST_REAL 0x04
#define USAGE_X 0x30
#define USAGE_Y 0x31
#define USAGE_WHEEL 0x38
#define USAGE_AC_PAN 0x238

typedef struct {
    uint16_t page;
    int32_t logical_min;
    uint32_t report_size;
    uint32_t report_count;
    uint8_t report_id;
} globals_t;

static hid_report_info_t *report_info(hid_layout_t *l, uint8_t id)
{
    for (int i = 0; i < l->report_count; i++) {
        if (l->reports[i].id == id) {
            return &l->reports[i];
        }
    }
    if (l->report_count >= HID_REPORT_MAX) {
        l->truncated = true;
        return NULL;
    }
    hid_report_info_t *r = &l->reports[l->report_count++];
    r->id = id;
    r->bits = 0;
    return r;
}

void hid_layout_parse(const uint8_t *map, size_t len, hid_layout_t *out)
{
    memset(out, 0, sizeof(*out));
    globals_t g = {0};
    globals_t stack[STACK_MAX];
    int sp = 0;
    /* Local: reset at every Main item */
    uint16_t usages[6];
    uint16_t usage_pages[6];
    uint8_t usage_count = 0;
    bool have_min = false;
    uint16_t usage_min = 0;
    uint16_t usage_min_page = 0;
    /* Output report lengths so far, per report ID (separate from input reports) */
    uint8_t out_ids[HID_REPORT_MAX];
    uint16_t out_bits[HID_REPORT_MAX];
    int out_n = 0;

    size_t i = 0;
    while (i < len) {
        uint8_t prefix = map[i];
        if (prefix == LONG_ITEM) {
            if (i + 2 >= len) {
                break;
            }
            i += 3 + map[i + 1];
            continue;
        }
        uint8_t size = prefix & 0x03;
        if (size == 3) {
            size = 4;
        }
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = prefix >> 4;
        if (i + 1 + size > len) {
            break;
        }
        uint32_t data = 0;
        for (uint8_t b = 0; b < size; b++) {
            data |= (uint32_t)map[i + 1 + b] << (8 * b);
        }
        int32_t sdata = (int32_t)data;
        if (size == 1) {
            sdata = (int8_t)data;
        } else if (size == 2) {
            sdata = (int16_t)data;
        }
        i += 1 + size;

        if (type == TYPE_GLOBAL) {
            switch (tag) {
            case TAG_USAGE_PAGE:
                g.page = (uint16_t)data;
                break;
            case TAG_LOGICAL_MIN:
                g.logical_min = sdata;
                break;
            case TAG_REPORT_SIZE:
                g.report_size = data;
                break;
            case TAG_REPORT_ID:
                g.report_id = (uint8_t)data;
                break;
            case TAG_REPORT_COUNT:
                g.report_count = data;
                break;
            case TAG_PUSH:
                if (sp < STACK_MAX) {
                    stack[sp++] = g;
                }
                break;
            case TAG_POP:
                if (sp > 0) {
                    g = stack[--sp];
                }
                break;
            default:
                break;
            }
        } else if (type == TYPE_LOCAL) {
            /* For a 4-byte Usage, the upper 16 bits are the Usage Page (extended Usage) */
            uint16_t page = size == 4 ? (uint16_t)(data >> 16) : g.page;
            if (tag == TAG_USAGE && usage_count < 6) {
                usages[usage_count] = (uint16_t)data;
                usage_pages[usage_count] = page;
                usage_count++;
            } else if (tag == TAG_USAGE_MIN) {
                have_min = true;
                usage_min = (uint16_t)data;
                usage_min_page = page;
            }
        } else if (type == TYPE_MAIN) {
            if (tag == TAG_INPUT) {
                hid_report_info_t *r = report_info(out, g.report_id);
                uint32_t total = g.report_size * g.report_count;
                if (r && !(data & INPUT_CONSTANT) && g.report_size > 0 && g.report_size <= 32 &&
                    g.report_count > 0) {
                    if (out->field_count < HID_FIELD_MAX) {
                        hid_field_t *f = &out->fields[out->field_count++];
                        f->report_id = g.report_id;
                        f->bit_offset = r->bits;
                        f->bit_size = (uint8_t)g.report_size;
                        f->count = (uint16_t)g.report_count;
                        f->logical_min = g.logical_min;
                        f->array = !(data & INPUT_VARIABLE);
                        f->relative = (data & INPUT_RELATIVE) != 0;
                        f->has_usage_range = have_min;
                        f->usage_min = usage_min;
                        f->usage_count = usage_count;
                        memcpy(f->usages, usages, sizeof(usages));
                        if (usage_count > 0 && !(f->array && have_min)) {
                            f->page = usage_pages[0];
                        } else if (have_min) {
                            f->page = usage_min_page;
                        } else {
                            f->page = g.page;
                        }
                    } else {
                        out->truncated = true;
                    }
                }
                if (r) {
                    r->bits = (uint16_t)(r->bits + total);
                }
            }
            if (tag == TAG_OUTPUT) {
                int k = 0;
                while (k < out_n && out_ids[k] != g.report_id) {
                    k++;
                }
                if (k == out_n && out_n < HID_REPORT_MAX) {
                    out_ids[out_n] = g.report_id;
                    out_bits[out_n++] = 0;
                }
                if (k < out_n) {
                    uint16_t page = usage_count > 0 ? usage_pages[0] : (have_min ? usage_min_page : g.page);
                    bool led = page == HID_PAGE_LED && !(data & INPUT_CONSTANT) && (data & INPUT_VARIABLE) &&
                               g.report_size == 1 && (usage_count > 0 || have_min);
                    if (led && (!out->led.present || out->led.report_id == g.report_id)) {
                        if (!out->led.present) {
                            out->led.present = true;
                            out->led.report_id = g.report_id;
                            for (int n = 0; n < HID_LED_COUNT; n++) {
                                out->led.bit[n] = -1;
                            }
                        }
                        for (uint32_t n = 0; n < g.report_count; n++) {
                            uint16_t usage = have_min ? (uint16_t)(usage_min + n)
                                                      : usages[n < usage_count ? n : usage_count - 1];
                            if (usage >= 1 && usage <= HID_LED_COUNT) {
                                out->led.bit[usage - 1] = (int16_t)(out_bits[k] + n);
                            }
                        }
                    }
                    out_bits[k] = (uint16_t)(out_bits[k] + g.report_size * g.report_count);
                }
            }
            /* Local items are reset at every Main item (HID 1.11 6.2.2.8) */
            usage_count = 0;
            have_min = false;
        }
    }
    for (int k = 0; k < out_n && out->led.present; k++) {
        if (out_ids[k] == out->led.report_id) {
            out->led.bytes = (uint8_t)((out_bits[k] + 7) / 8);
        }
    }
}

size_t hid_led_report(const hid_layout_t *layout, uint8_t host_led, uint8_t *out, size_t out_len)
{
    const hid_led_t *led = &layout->led;
    if (!led->present || led->bytes == 0 || led->bytes > out_len) {
        return 0;
    }
    memset(out, 0, led->bytes);
    for (int n = 0; n < HID_LED_COUNT; n++) {
        if (led->bit[n] >= 0 && led->bit[n] < led->bytes * 8 && ((host_led >> n) & 1)) {
            out[led->bit[n] >> 3] |= (uint8_t)(1u << (led->bit[n] & 7));
        }
    }
    return led->bytes;
}

uint16_t hid_layout_report_bits(const hid_layout_t *layout, uint8_t report_id)
{
    for (int i = 0; i < layout->report_count; i++) {
        if (layout->reports[i].id == report_id) {
            return layout->reports[i].bits;
        }
    }
    return 0;
}

/* Read size bits in little-endian bit order. false if past the end of the data */
static bool get_bits(const uint8_t *d, size_t len, uint32_t off, uint8_t size, bool is_signed, int32_t *out)
{
    if (((off + size + 7) >> 3) > len) {
        return false;
    }
    uint32_t v = 0;
    for (uint8_t k = 0; k < size; k++) {
        uint32_t p = off + k;
        if ((d[p >> 3] >> (p & 7)) & 1) {
            v |= 1u << k;
        }
    }
    if (is_signed && size < 32 && (v & (1u << (size - 1)))) {
        v |= ~0u << size;
    }
    *out = (int32_t)v;
    return true;
}

static void add_key(hid_input_t *o, uint32_t usage)
{
    if (usage >= KEY_FIRST_REAL && usage < 256) {
        o->keys[usage >> 3] |= (uint8_t)(1u << (usage & 7));
    }
}

static void add_consumer(hid_input_t *o, uint32_t usage)
{
    if (usage == 0 || o->consumer_count >= HID_CONSUMER_MAX) {
        return;
    }
    for (int i = 0; i < o->consumer_count; i++) {
        if (o->consumer[i] == usage) {
            return;
        }
    }
    o->consumer[o->consumer_count++] = (uint16_t)usage;
}

/* Usage of Variable field n: the listed Usages (the last repeats if too few) or the range */
static uint32_t variable_usage(const hid_field_t *f, uint16_t n)
{
    if (f->usage_count > 0) {
        return f->usages[n < f->usage_count ? n : f->usage_count - 1];
    }
    return (uint32_t)f->usage_min + n;
}

bool hid_decode(const hid_layout_t *layout, uint8_t report_id, const uint8_t *data, size_t len, hid_input_t *out)
{
    memset(out, 0, sizeof(*out));
    uint16_t bits = hid_layout_report_bits(layout, report_id);
    if (bits == 0) {
        return false;
    }
    size_t need = (bits + 7) / 8;
    if (report_id != 0 && len == need + 1 && data[0] == report_id) {
        data++;
        len--;
    }
    for (int i = 0; i < layout->field_count; i++) {
        const hid_field_t *f = &layout->fields[i];
        if (f->report_id != report_id) {
            continue;
        }
        bool is_signed = f->logical_min < 0;
        for (uint16_t n = 0; n < f->count; n++) {
            int32_t v;
            if (!get_bits(data, len, (uint32_t)f->bit_offset + (uint32_t)n * f->bit_size, f->bit_size, is_signed, &v)) {
                break;
            }
            uint32_t usage;
            if (f->array) {
                if (v == 0 || v < f->logical_min) {
                    continue;   /* 0 in an array means "nothing pressed" */
                }
                usage = (uint32_t)f->usage_min + (uint32_t)(v - f->logical_min);
            } else {
                usage = variable_usage(f, n);
            }
            switch (f->page) {
            case HID_PAGE_KEYBOARD:
                out->has_keys = true;
                if (f->array || v) {
                    add_key(out, usage);
                }
                break;
            case HID_PAGE_BUTTON:
                out->has_buttons = true;
                if ((f->array || v) && usage >= 1 && usage <= 8) {
                    out->buttons |= (uint8_t)(1u << (usage - 1));
                }
                break;
            case HID_PAGE_DESKTOP:
                if (!f->array && f->relative) {
                    out->has_motion = true;
                    if (usage == USAGE_X) {
                        out->dx += v;
                    } else if (usage == USAGE_Y) {
                        out->dy += v;
                    } else if (usage == USAGE_WHEEL) {
                        out->wheel += v;
                    }
                }
                break;
            case HID_PAGE_CONSUMER:
                if (!f->array && f->relative && usage == USAGE_AC_PAN) {
                    out->has_motion = true;
                    out->pan += v;
                } else {
                    out->has_consumer = true;
                    if (f->array || v) {
                        add_consumer(out, usage);
                    }
                }
                break;
            default:
                break;
            }
        }
    }
    return true;
}
