#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hid_map.h"

#define PAGE_GENERIC_DESKTOP 0x01
#define PAGE_CONSUMER 0x0C

/* 항목 종류 (HID 1.11 6.2.2.2 Short Items) */
#define TYPE_MAIN 0
#define TYPE_GLOBAL 1
#define TYPE_LOCAL 2
#define TAG_MAIN_COLLECTION 0xA
#define TAG_MAIN_END_COLLECTION 0xC
#define TAG_GLOBAL_USAGE_PAGE 0x0
#define TAG_GLOBAL_PUSH 0xA
#define TAG_GLOBAL_POP 0xB
#define TAG_LOCAL_USAGE 0x0
#define COLLECTION_APPLICATION 0x01
#define LONG_ITEM_PREFIX 0xFE
#define PAGE_STACK 4

static uint8_t classify(uint16_t page, uint16_t usage)
{
    if (page == PAGE_GENERIC_DESKTOP) {
        switch (usage) {
        case 0x06:
        case 0x07:
            return HID_KIND_KEYBOARD;
        case 0x01:
        case 0x02:
            return HID_KIND_MOUSE;
        case 0x04:
        case 0x05:
            return HID_KIND_GAME;
        case 0x80:
            return HID_KIND_SYSTEM;
        default:
            return HID_KIND_OTHER;
        }
    }
    if (page == PAGE_CONSUMER && usage == 0x01) {
        return HID_KIND_CONSUMER;
    }
    return HID_KIND_OTHER;
}

uint8_t hid_map_kinds(const uint8_t *map, size_t len)
{
    uint8_t kinds = 0;
    uint16_t page = 0;
    uint16_t page_stack[PAGE_STACK];
    int sp = 0;
    bool have_usage = false;
    uint32_t usage = 0;     /* 4바이트 Usage면 위 16비트가 Usage Page다 */
    uint8_t usage_size = 0;
    int depth = 0;
    size_t i = 0;

    while (i < len) {
        uint8_t prefix = map[i];
        if (prefix == LONG_ITEM_PREFIX) {
            if (i + 2 >= len) {
                break;
            }
            i += 3 + map[i + 1];   /* Long Item: 크기 바이트 + 태그 바이트 + 데이터 */
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
        i += 1 + size;

        if (type == TYPE_GLOBAL) {
            if (tag == TAG_GLOBAL_USAGE_PAGE) {
                page = (uint16_t)data;
            } else if (tag == TAG_GLOBAL_PUSH && sp < PAGE_STACK) {
                page_stack[sp++] = page;
            } else if (tag == TAG_GLOBAL_POP && sp > 0) {
                page = page_stack[--sp];
            }
        } else if (type == TYPE_LOCAL) {
            if (tag == TAG_LOCAL_USAGE && !have_usage) {
                have_usage = true;
                usage = data;
                usage_size = size;
            }
        } else if (type == TYPE_MAIN) {
            if (tag == TAG_MAIN_COLLECTION) {
                if (depth == 0 && data == COLLECTION_APPLICATION && have_usage) {
                    uint16_t u_page = usage_size == 4 ? (uint16_t)(usage >> 16) : page;
                    kinds |= classify(u_page, (uint16_t)(usage & 0xFFFF));
                }
                depth++;
            } else if (tag == TAG_MAIN_END_COLLECTION && depth > 0) {
                depth--;
            }
            /* Local 항목은 Main 항목마다 초기화된다 (HID 1.11 6.2.2.8) */
            have_usage = false;
        }
    }
    return kinds;
}

void hid_kinds_json(uint8_t kinds, char *out, size_t len)
{
    static const struct {
        uint8_t bit;
        const char *name;
    } names[] = {
        {HID_KIND_KEYBOARD, "keyboard"}, {HID_KIND_MOUSE, "mouse"},   {HID_KIND_CONSUMER, "consumer"},
        {HID_KIND_SYSTEM, "system"},     {HID_KIND_GAME, "game"},     {HID_KIND_OTHER, "other"},
    };
    size_t n = snprintf(out, len, "[");
    bool first = true;
    for (size_t k = 0; k < sizeof(names) / sizeof(names[0]) && n + 1 < len; k++) {
        if (kinds & names[k].bit) {
            n += snprintf(out + n, len - n, "%s\"%s\"", first ? "" : ",", names[k].name);
            first = false;
        }
    }
    if (n + 1 < len) {
        snprintf(out + n, len - n, "]");
    }
}
