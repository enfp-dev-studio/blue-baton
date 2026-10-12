/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Unit tests for hid_map.c. Runs on a Mac (or Linux) without a board.
 *   firmware/test/run.sh
 * Feeds two Report Maps captured from real hardware, plus forms allowed by HID 1.11 (4-byte Usage, Push/Pop,
 * Long Item, nested collections, malformed input).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/hid_map.h"

static int s_fail;

static size_t from_hex(const char *hex, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (hex[0] && hex[1] && n < max) {
        unsigned v;
        sscanf(hex, "%2x", &v);
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return n;
}

static void check(const char *name, const char *hex, uint8_t want)
{
    uint8_t map[600];
    size_t len = from_hex(hex, map, sizeof(map));
    uint8_t got = hid_map_kinds(map, len);
    char gj[80];
    char wj[80];
    hid_kinds_json(got, gj, sizeof(gj));
    hid_kinds_json(want, wj, sizeof(wj));
    if (got == want) {
        printf("ok    %-34s %s\n", name, gj);
    } else {
        printf("FAIL  %-34s got %s want %s\n", name, gj, wj);
        s_fail++;
    }
}

int main(void)
{
    /* Real hardware: Xiaoxin Mplus 5.0 (66 bytes) */
    check("real mouse (Xiaoxin Mplus 5.0)",
          "05010902A10185010901A1000509190129051500250195057501810295017503810105010930093116018026FF7F75109502"
          "810609381581257F750895018106C0C0",
          HID_KIND_MOUSE);

    /* Real hardware: ABKO AK84BT-1 (236 bytes). Mouse, consumer control, keyboard (6-key), keyboard (NKRO), vendor consumer collection */
    check("real keyboard (ABKO AK84BT-1)",
          "05010902A10185010901A100050919012905150025019505750181029501750381010501093009311581257F750895028106"
          "09381581257F750895018106050C0A38021581257F750895018106C0C0050C0901A101850275109501150126FF0319012AFF"
          "038160C005010906A1018503050719E029E71500250175019508810295017508810195057501050819012905910295017503"
          "910195067508150025E70507190029E78100C005010906A101850475019578050715002501190029788102C0050C0901A101"
          "85051500250175019503810175019501050C09B8810206FF0009038102750195038101C0",
          HID_KIND_KEYBOARD | HID_KIND_MOUSE | HID_KIND_CONSUMER);

    check("gamepad", "05010905A101C0", HID_KIND_GAME);
    check("joystick", "05010904A101C0", HID_KIND_GAME);
    check("system control", "05010980A101C0", HID_KIND_SYSTEM);
    check("keypad counts as keyboard", "05010907A101C0", HID_KIND_KEYBOARD);
    check("pointer counts as mouse", "05010901A101C0", HID_KIND_MOUSE);
    /* 4-byte Usage: the upper 16 bits are the Usage Page (HID 1.11 6.2.2.8) */
    check("extended usage page in usage", "0B06000100A101C0", HID_KIND_KEYBOARD);
    /* Push saves Consumer, switch to Generic Desktop, then Pop restores it */
    check("push/pop usage page", "050CA40501B40901A101C0", HID_KIND_CONSUMER);
    /* A Physical collection inside an Application is not a device kind */
    check("nested collection only top level", "05010902A1010901A100C0C0", HID_KIND_MOUSE);
    /* Even with an Application nested inside a top-level Application, only the top level counts */
    check("application inside application", "05010902A101050C0901A101C0C0", HID_KIND_MOUSE);
    /* Long Items (0xFE) are skipped */
    check("long item skipped", "FE0210AABB05010906A101C0", HID_KIND_KEYBOARD);
    check("vendor collection is other", "0600FF0901A101C0", HID_KIND_OTHER);
    /* A collection opened without a Usage is not classified */
    check("collection without usage", "0501A101C0", 0);
    /* Local items reset after every Main item: a Collection after an Input does not use the earlier Usage */
    check("local items reset after main", "050109068100A101C0", 0);
    /* Malformed input: stop at an item whose data is short (must not read past the end) */
    check("truncated item", "050109", 0);
    check("truncated long item", "FE05", 0);
    check("empty", "", 0);

    printf("%s (%d failed)\n", s_fail ? "FAILED" : "ALL PASSED", s_fail);
    return s_fail ? 1 : 0;
}
