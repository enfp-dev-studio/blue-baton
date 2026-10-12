/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Unit tests for hid_input.c (input normalization) and router.c (merging, switch combos). Runs on a Mac without a board.
 * Uses Report Maps captured from real hardware (Xiaoxin Mplus 5.0 mouse, ABKO AK84BT-1 keyboard).
 */
#include <stdio.h>
#include <string.h>

#include "../main/hid_input.h"
#include "../main/host_report.h"
#include "../main/router.h"

static int s_fail;
static int s_pass;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (cond) {                                         \
            s_pass++;                                       \
        } else {                                            \
            s_fail++;                                       \
            printf("FAIL  %s:%d  ", __FILE__, __LINE__);    \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
        }                                                   \
    } while (0)

static const char *MOUSE_MAP =
    "05010902A10185010901A1000509190129051500250195057501810295017503810105010930093116018026FF7F75109502"
    "810609381581257F750895018106C0C0";
static const char *KB_MAP =
    "05010902A10185010901A100050919012905150025019505750181029501750381010501093009311581257F750895028106"
    "09381581257F750895018106050C0A38021581257F750895018106C0C0050C0901A101850275109501150126FF0319012AFF"
    "038160C005010906A1018503050719E029E71500250175019508810295017508810195057501050819012905910295017503"
    "910195067508150025E70507190029E78100C005010906A101850475019578050715002501190029788102C0050C0901A101"
    "85051500250175019503810175019501050C09B8810206FF0009038102750195038101C0";

static size_t hex(const char *s, uint8_t *out, size_t max)
{
    size_t n = 0;
    while (s[0] && s[1] && n < max) {
        unsigned v;
        sscanf(s, "%2x", &v);
        out[n++] = (uint8_t)v;
        s += 2;
    }
    return n;
}

static hid_layout_t s_mouse;
static hid_layout_t s_kb;

static void load_layouts(void)
{
    uint8_t buf[600];
    size_t n = hex(MOUSE_MAP, buf, sizeof(buf));
    hid_layout_parse(buf, n, &s_mouse);
    n = hex(KB_MAP, buf, sizeof(buf));
    hid_layout_parse(buf, n, &s_kb);
}

static bool decode_hex(const hid_layout_t *l, uint8_t id, const char *data, hid_input_t *out)
{
    uint8_t buf[64];
    size_t n = hex(data, buf, sizeof(buf));
    return hid_decode(l, id, buf, n, out);
}

/* ---------- input normalization ---------- */

static void test_input(void)
{
    hid_input_t in;

    CHECK(!s_mouse.truncated && !s_kb.truncated, "layouts fit");
    CHECK(hid_layout_report_bits(&s_mouse, 1) == 48, "mouse report 1 is 48 bits, got %u", hid_layout_report_bits(&s_mouse, 1));
    CHECK(hid_layout_report_bits(&s_kb, 4) == 120, "kb NKRO report is 120 bits");
    CHECK(hid_layout_report_bits(&s_kb, 3) == 64, "kb 6KRO report is 64 bits");

    /* LED output report (real keyboard: ID 3, 5 LED bits + 3 padding bits) */
    CHECK(s_kb.led.present && s_kb.led.report_id == 3 && s_kb.led.bytes == 1, "kb LED report id=%u bytes=%u",
          s_kb.led.report_id, s_kb.led.bytes);
    CHECK(s_kb.led.bit[0] == 0 && s_kb.led.bit[1] == 1 && s_kb.led.bit[4] == 4, "kb LED bits in boot order");
    CHECK(!s_mouse.led.present, "mouse has no LED report");
    uint8_t led[4];
    CHECK(hid_led_report(&s_kb, 0x02, led, sizeof(led)) == 1 && led[0] == 0x02, "caps lock goes to bit 1");
    CHECK(hid_led_report(&s_mouse, 0x02, led, sizeof(led)) == 0, "no LED report, nothing to write");
    {
        /* LEDs listed one by one in another order: Caps Lock first, then Num Lock, after 4 constant bits */
        static const uint8_t map[] = {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x07,
                                      0x75, 0x04, 0x95, 0x01, 0x91, 0x01,
                                      0x05, 0x08, 0x09, 0x02, 0x09, 0x01, 0x75, 0x01, 0x95, 0x02, 0x91, 0x02,
                                      0x75, 0x02, 0x95, 0x01, 0x91, 0x01, 0xC0};
        hid_layout_t l;
        hid_layout_parse(map, sizeof(map), &l);
        CHECK(l.led.present && l.led.report_id == 7 && l.led.bytes == 1 && l.led.bit[1] == 4 && l.led.bit[0] == 5 &&
              l.led.bit[2] == -1, "listed LEDs: caps=%d num=%d", l.led.bit[1], l.led.bit[0]);
        CHECK(hid_led_report(&l, 0x03, led, sizeof(led)) == 1 && led[0] == 0x30, "num+caps map to bits 5 and 4");
    }

    /* Real-hardware mouse: left button, X +5, Y -5, wheel +1 */
    CHECK(decode_hex(&s_mouse, 1, "010500FBFF01", &in), "mouse decodes");
    CHECK(in.buttons == 0x01 && in.dx == 5 && in.dy == -5 && in.wheel == 1, "mouse fields b=%u dx=%d dy=%d w=%d",
          in.buttons, (int)in.dx, (int)in.dy, (int)in.wheel);

    /* When the report arrives with the Report ID byte prepended */
    CHECK(decode_hex(&s_mouse, 1, "01010500FBFF01", &in) && in.dx == 5, "report id prefix stripped");

    /* Mouse report inside the keyboard (ID 1): buttons, 8-bit X/Y/wheel, horizontal wheel (AC Pan) */
    CHECK(decode_hex(&s_kb, 1, "0105FB01FF", &in), "kb mouse decodes");
    CHECK(in.buttons == 1 && in.dx == 5 && in.dy == -5 && in.wheel == 1 && in.pan == -1, "kb mouse pan=%d", (int)in.pan);

    /* NKRO bitmap (ID 4): bit number = Usage. 'h' (0x0B) and Space (0x2C) */
    uint8_t nkro[15] = {0};
    nkro[0x0B >> 3] |= 1 << (0x0B & 7);
    nkro[0x2C >> 3] |= 1 << (0x2C & 7);
    CHECK(hid_decode(&s_kb, 4, nkro, sizeof(nkro), &in), "nkro decodes");
    CHECK(in.has_keys && hid_key_down(in.keys, 0x0B) && hid_key_down(in.keys, 0x2C) && !hid_key_down(in.keys, 0x04),
          "nkro keys");

    /* 6-key array (ID 3): LShift + 'a' */
    CHECK(decode_hex(&s_kb, 3, "0200040000000000", &in), "6kro decodes");
    CHECK(hid_key_down(in.keys, 0xE1) && hid_key_down(in.keys, 0x04), "6kro shift+a");

    /* ErrorRollOver (0x01) is not a key */
    CHECK(decode_hex(&s_kb, 3, "0000010101010101", &in) && !hid_key_down(in.keys, 0x01), "rollover ignored");

    /* Media key array (ID 2): Volume Up 0xE9 */
    CHECK(decode_hex(&s_kb, 2, "E900", &in), "consumer decodes");
    CHECK(in.has_consumer && in.consumer_count == 1 && in.consumer[0] == 0xE9, "volume up");
    /* Release: every array slot empty still says "no media key" (HID 1.11 6.2.2.5) */
    CHECK(decode_hex(&s_kb, 2, "0000", &in) && in.has_consumer && in.consumer_count == 0, "media key release");

    /* Unknown Report ID, short data */
    CHECK(!decode_hex(&s_kb, 9, "00", &in), "unknown report id");
    CHECK(decode_hex(&s_mouse, 1, "01", &in) && in.buttons == 1 && in.dx == 0, "short data reads what exists");

    /* With fewer Usages than Report Count, the last Usage repeats (HID 1.11 6.2.2.8): one X, three fields */
    hid_layout_t l;
    uint8_t m[64];
    size_t n = hex("05010902A10109301581257F750895038106C0", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(decode_hex(&l, 0, "010203", &in) && in.dx == 6, "last usage repeats (dx=%d)", (int)in.dx);

    /* Consumer bitmap with 8 Usages listed one by one: bit 6 is Volume Down (0xEA), bit 7 Eject (0xB8) */
    n = hex("050C0901A101150025017501950809B509B609B709CD09E209E909EA09B88102C0", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(decode_hex(&l, 0, "40", &in) && in.consumer_count == 1 && in.consumer[0] == 0xEA, "7th listed usage");
    CHECK(decode_hex(&l, 0, "80", &in) && in.consumer_count == 1 && in.consumer[0] == 0xB8, "8th listed usage");

    /* Consumer array with listed Usages: value v selects the (v - Logical Minimum)-th one */
    n = hex("050C0901A101150125037508950109E909EA09E28100C0", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(decode_hex(&l, 0, "02", &in) && in.consumer_count == 1 && in.consumer[0] == 0xEA, "array picks from list");
    CHECK(decode_hex(&l, 0, "00", &in) && in.has_consumer && in.consumer_count == 0, "array value 0 is none here");

    /* System Control as a 2-bit array (Logical 1-3 = Power Down, Sleep, Wake Up) */
    n = hex("05010980A10185051981298315012503750295018100750695018103C0", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(decode_hex(&l, 5, "02", &in) && in.has_system && in.system == 0x02, "system sleep (array)");
    CHECK(decode_hex(&l, 5, "00", &in) && in.has_system && in.system == 0, "system release (array)");

    /* Push/Pop: Report Size 8 is saved and then restored */
    n = hex("05010902A10175089501A47501B409308106C0", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(l.field_count == 1 && l.fields[0].bit_size == 8, "push/pop restores report size");

    /* Stops on a malformed Report Map */
    n = hex("050109", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(l.field_count == 0, "broken map");
}

/* ---------- combo parsing ---------- */

static void test_combo_parse(void)
{
    combo_t c;
    CHECK(combo_parse("ctrl+alt+1", &c) && c.mods == (COMBO_CTRL | COMBO_ALT) && c.key == 0x1E, "ctrl+alt+1");
    CHECK(combo_parse("Cmd + Shift + F1", &c) && c.mods == (COMBO_GUI | COMBO_SHIFT) && c.key == 0x3A, "cmd+shift+f1");
    CHECK(combo_parse("ctrl+alt+0x3a", &c) && c.key == 0x3A, "hex usage");
    CHECK(combo_parse("option+ctrl+space", &c) && c.mods == (COMBO_ALT | COMBO_CTRL) && c.key == 0x2C, "option alias");
    CHECK(combo_parse("ctrl+f13", &c) && c.key == 0x68, "f13");
    CHECK(!combo_parse("1", &c), "single key without modifier is refused");
    CHECK(!combo_parse("ctrl+alt", &c), "modifiers only is refused");
    CHECK(!combo_parse("ctrl+a+b", &c), "two normal keys refused");
    CHECK(!combo_parse("ctrl+banana", &c), "unknown key refused");
    CHECK(!combo_parse("ctrl+0xE0", &c), "modifier usage as key refused");

    combo_t list[ROUTER_HOST_MAX];
    CHECK(combo_parse_list("ctrl+alt+1,ctrl+alt+2,ctrl+alt+3", list) && list[2].key == 0x20, "list of 3");
    CHECK(combo_parse_list(",,ctrl+3", list) && !list[0].valid && !list[1].valid && list[2].valid, "blank entries");
    CHECK(!combo_parse_list("ctrl+1,ctrl+2,ctrl+3,ctrl+4", list), "too many hosts");
    CHECK(!combo_parse_list("ctrl+1,nope", list), "bad entry fails the list");
}

/* ---------- router ---------- */

/* Builds one keyboard 6KRO report (ID 3) and feeds it to slot. mods are the modifier bits, keys the non-modifier key Usages */
static void press(uint8_t slot, uint8_t mods, const uint8_t *keys, int nkeys, router_out_t *out, int *from)
{
    char data[17];
    uint8_t raw[8] = {mods, 0};
    for (int i = 0; i < nkeys && i < 6; i++) {
        raw[2 + i] = keys[i];
    }
    for (int i = 0; i < 8; i++) {
        snprintf(data + i * 2, sizeof(data) - i * 2, "%02X", raw[i]);
    }
    hid_input_t in;
    decode_hex(&s_kb, 3, data, &in);
    router_input(slot, 3, &in, out, from);
}

static bool out_empty(const router_out_t *o)
{
    for (int i = 0; i < 32; i++) {
        if (o->keys[i]) {
            return false;
        }
    }
    return true;
}

#define LCTRL 0x01
#define LSHIFT 0x02
#define LALT 0x04
#define RCTRL 0x10
#define RALT 0x40
#define K1 0x1E
#define K2 0x1F
#define KA 0x04
#define KX 0x1B

static void setup_router(void)
{
    combo_t list[ROUTER_HOST_MAX];
    combo_parse_list("ctrl+alt+1,ctrl+alt+2,ctrl+alt+3", list);
    router_init();
    router_set_combos(list);
}

static void test_router(void)
{
    router_out_t o;
    int from;
    uint8_t k[2];

    /* Normal switch: press Ctrl, Alt, then 2. Combo modifiers are held, so nothing reaches the previous host */
    setup_router();
    press(0, LCTRL, NULL, 0, &o, &from);
    CHECK(from == -1 && out_empty(&o) && !o.keys_changed && !o.pre_valid, "ctrl is held back, nothing sent");
    press(0, LCTRL | LALT, NULL, 0, &o, &from);
    CHECK(out_empty(&o) && !o.keys_changed && !o.pre_valid, "alt is held back too");
    k[0] = K2;
    press(0, LCTRL | LALT, k, 1, &o, &from);
    CHECK(from == 0 && router_active_host() == 1, "switched 0 -> 1 (from=%d active=%d)", from, router_active_host());
    CHECK(out_empty(&o) && !o.pre_valid && !o.keys_changed, "old host never saw ctrl or alt, new host sees nothing");
    press(0, LCTRL | LALT, NULL, 0, &o, &from);
    CHECK(out_empty(&o) && from == -1, "still held modifiers stay blocked after releasing 2");
    press(0, 0, NULL, 0, &o, &from);
    CHECK(out_empty(&o), "all released");
    k[0] = KA;
    press(0, LCTRL, k, 1, &o, &from);
    CHECK(hid_key_down(o.keys, KA) && hid_key_down(o.keys, 0xE0), "after release, ctrl+a goes to host 2 normally");
    press(0, 0, NULL, 0, &o, &from);

    /* No switch while another non-modifier key is held */
    setup_router();
    k[0] = KX;
    k[1] = K2;
    press(0, LCTRL | LALT, k, 1, &o, &from);
    press(0, LCTRL | LALT, k, 2, &o, &from);
    CHECK(from == -1 && router_active_host() == 0 && hid_key_down(o.keys, K2), "extra key held: no switch, keys pass");

    /* One extra modifier makes it a different combo */
    setup_router();
    k[0] = K2;
    press(0, LCTRL | LALT | LSHIFT, k, 1, &o, &from);
    CHECK(from == -1 && router_active_host() == 0, "ctrl+alt+shift+2 is not ctrl+alt+2");

    /* Right-hand modifiers count as the same set */
    setup_router();
    k[0] = K2;
    press(0, RCTRL | RALT, NULL, 0, &o, &from);
    press(0, RCTRL | RALT, k, 1, &o, &from);
    CHECK(from == 0 && router_active_host() == 1, "right ctrl+alt+2 switches");

    /* Combo for the current host: no switch, but the combo does not leak */
    setup_router();
    k[0] = K1;
    press(0, LCTRL | LALT, k, 1, &o, &from);
    CHECK(from == -1 && router_active_host() == 0 && out_empty(&o), "combo for active host is swallowed");

    /* Combo keys split across two devices are judged on the merged state (two keyboards) */
    setup_router();
    press(0, LCTRL | LALT, NULL, 0, &o, &from);
    k[0] = 0x20; /* '3' */
    press(1, 0, k, 1, &o, &from);
    CHECK(from == 0 && router_active_host() == 2, "combo across two keyboards");

    /* When a device disconnects, its keys are treated as released */
    setup_router();
    k[0] = KA;
    press(0, 0, k, 1, &o, &from);
    CHECK(hid_key_down(o.keys, KA), "a held");
    router_device_gone(0, &o);
    CHECK(out_empty(&o) && o.keys_changed, "device gone releases its keys");

    /* Mouse: motion passes through, buttons are merged */
    setup_router();
    hid_input_t in;
    decode_hex(&s_mouse, 1, "010500FBFF00", &in);
    router_input(1, 1, &in, &o, &from);
    CHECK(o.motion && o.dx == 5 && o.dy == -5 && o.buttons == 1 && o.buttons_changed, "mouse passes through");
    decode_hex(&s_mouse, 1, "010000000000", &in);
    router_input(1, 1, &in, &o, &from);
    CHECK(!o.motion && !o.buttons_changed, "no motion, button unchanged");

    /* Ctrl+C: send the held Ctrl first, then C */
    setup_router();
    press(0, LCTRL, NULL, 0, &o, &from);
    k[0] = 0x06; /* 'c' */
    press(0, LCTRL, k, 1, &o, &from);
    CHECK(o.pre_valid && hid_key_down(o.pre_keys, 0xE0) && !hid_key_down(o.pre_keys, 0x06), "pre: ctrl alone first");
    CHECK(hid_key_down(o.keys, 0xE0) && hid_key_down(o.keys, 0x06) && o.keys_changed, "then ctrl+c");
    press(0, 0, NULL, 0, &o, &from);
    CHECK(out_empty(&o) && o.keys_changed && !o.pre_valid, "all released");

    /* Ctrl tap: press and release back to back */
    setup_router();
    press(0, LCTRL, NULL, 0, &o, &from);
    press(0, 0, NULL, 0, &o, &from);
    CHECK(o.pre_valid && hid_key_down(o.pre_keys, 0xE0) && out_empty(&o) && o.keys_changed, "ctrl tap = down then up");

    /* Moving the mouse with Ctrl held sends Ctrl first (Ctrl+scroll zoom) */
    setup_router();
    press(0, LCTRL, NULL, 0, &o, &from);
    decode_hex(&s_mouse, 1, "000000000001", &in);
    router_input(1, 1, &in, &o, &from);
    CHECK(o.pre_valid && hid_key_down(o.pre_keys, 0xE0) && hid_key_down(o.keys, 0xE0) && o.motion && o.wheel == 1,
          "mouse wheel releases held ctrl first");

    /* A modifier not in any combo (Shift) is not held */
    setup_router();
    press(0, LSHIFT, NULL, 0, &o, &from);
    CHECK(hid_key_down(o.keys, 0xE1) && o.keys_changed && !o.pre_valid, "shift is sent at once");

    /* Ctrl pressed while a non-modifier key is down is sent at once (it cannot become a combo) */
    setup_router();
    k[0] = KX;
    press(0, 0, k, 1, &o, &from);
    press(0, LCTRL, k, 1, &o, &from);
    CHECK(hid_key_down(o.keys, 0xE0) && !o.pre_valid, "ctrl after a normal key is not held");

    /* Passkey entry: 6 digits + Backspace + Enter. Nothing is sent meanwhile */
    setup_router();
    router_passkey_begin();
    const uint8_t digits[] = {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x59 /* keypad 1 */, 0x2A /* BS */, 0x23 /* 6 */};
    bool leaked = false;
    for (size_t i = 0; i < sizeof(digits); i++) {
        k[0] = digits[i];
        press(0, 0, k, 1, &o, &from);
        leaked |= !out_empty(&o) || o.keys_changed;
        press(0, 0, NULL, 0, &o, &from);
        leaked |= !out_empty(&o);
    }
    CHECK(!leaked && o.pk_len == 6, "passkey digits collected without leaking (len=%u)", o.pk_len);
    k[0] = 0x28; /* Enter */
    press(0, 0, k, 1, &o, &from);
    CHECK(o.pk_event == ROUTER_PK_DONE && o.pk_code == 123456, "passkey done code=%u", (unsigned)o.pk_code);
    router_passkey_end();
    press(0, 0, k, 1, &o, &from);
    CHECK(out_empty(&o), "enter still held after passkey does not leak");
    press(0, 0, NULL, 0, &o, &from);
    k[0] = KA;
    press(0, 0, k, 1, &o, &from);
    CHECK(hid_key_down(o.keys, KA), "normal typing after passkey");

    setup_router();
    router_passkey_begin();
    k[0] = 0x29; /* Esc */
    press(0, 0, k, 1, &o, &from);
    CHECK(o.pk_event == ROUTER_PK_CANCEL, "esc cancels passkey");
    router_passkey_end();

    /* LED: after a switch, the LED value last written by the new host comes out */
    setup_router();
    router_set_host_led(1, 0x02); /* Caps Lock on host 2 */
    press(0, LCTRL | LALT, NULL, 0, &o, &from);
    k[0] = K2;
    press(0, LCTRL | LALT, k, 1, &o, &from);
    CHECK(from == 0 && o.led_changed && o.led == 0x02, "led follows the active host");

    /* The "release-all" sent to the previous host on a switch */
    router_release_report(&o);
    CHECK(out_empty(&o) && o.buttons == 0 && o.keys_changed && o.buttons_changed, "release report");
    /* Media key through the router: press, then the empty array releases it */
    setup_router();
    hid_input_t mi;
    decode_hex(&s_kb, 2, "E900", &mi);
    router_input(0, 2, &mi, &o, &from);
    CHECK(o.consumer_changed && o.consumer_count == 1 && o.consumer[0] == 0xE9, "media key pressed");
    decode_hex(&s_kb, 2, "0000", &mi);
    router_input(0, 2, &mi, &o, &from);
    CHECK(o.consumer_changed && o.consumer_count == 0, "media key released");

    /* System key: press, release, and released when the device goes away */
    setup_router();
    hid_input_t si = {0};
    si.has_system = true;
    si.system = 0x02;
    router_input(0, 5, &si, &o, &from);
    CHECK(o.system_changed && o.system == 0x02, "sleep pressed");
    si.system = 0;
    router_input(0, 5, &si, &o, &from);
    CHECK(o.system_changed && o.system == 0, "sleep released");
    si.system = 0x01;
    router_input(1, 5, &si, &o, &from);
    router_device_gone(1, &o);
    CHECK(o.system_changed && o.system == 0, "system key released when its device disconnects");
    router_release_report(&o);
    CHECK(o.system_changed && o.system == 0, "release-all includes system keys");
}

/* ---------- output reports (host side) ---------- */

static void test_host_report(void)
{
    hid_layout_t l;
    hid_layout_parse(HOST_REPORT_MAP, HOST_REPORT_MAP_LEN, &l);
    CHECK(HOST_REPORT_MAP_LEN == 294 && !l.truncated, "host map is 294 bytes and fits (len=%zu)", HOST_REPORT_MAP_LEN);
    CHECK(hid_layout_report_bits(&l, HOST_RID_KEYBOARD) == HOST_KB_LEN * 8, "keyboard report 8 bytes");
    CHECK(hid_layout_report_bits(&l, HOST_RID_MOUSE) == HOST_MOUSE_LEN * 8, "mouse report 7 bytes");
    CHECK(hid_layout_report_bits(&l, HOST_RID_CONSUMER) == HOST_CONSUMER_LEN * 8, "consumer report 4 bytes");
    CHECK(hid_layout_report_bits(&l, HOST_RID_SYSTEM) == HOST_SYSTEM_LEN * 8, "system report 1 byte");
    CHECK(hid_layout_report_bits(&l, HOST_RID_PEN) == HOST_PEN_LEN * 8, "pen report 9 bytes");

    /* Pen: tip down and in range at the center, half pressure, tilt clamped to 90 degrees */
    host_pen_t pen = {.tip = true, .in_range = true, .x = 16384, .y = 40000, .pressure = 2048, .tilt_x = -100, .tilt_y = 30};
    uint8_t pr[HOST_PEN_LEN];
    host_pen_report(&pen, pr);
    CHECK(pr[0] == 0x11 && pr[1] == 0x00 && pr[2] == 0x40 && pr[3] == 0xFF && pr[4] == 0x7F, "pen bits and position (y clamped)");
    CHECK(pr[5] == 0x00 && pr[6] == 0x08 && (int8_t)pr[7] == -90 && pr[8] == 30, "pen pressure and tilt");

    /* Keyboard: Shift + a + LANG1 (0x90). Round trip: decoding the built report with our Report Map gives the same keys */
    uint8_t keys[32] = {0};
    keys[0xE1 >> 3] |= 1 << (0xE1 & 7);
    keys[0x04 >> 3] |= 1 << (0x04 & 7);
    keys[0x90 >> 3] |= 1 << (0x90 & 7);
    uint8_t kb[HOST_KB_LEN];
    host_kb_report(keys, kb);
    CHECK(kb[0] == 0x02 && kb[1] == 0 && kb[2] == 0x04 && kb[3] == 0x90 && kb[4] == 0, "kb bytes");
    hid_input_t in;
    CHECK(hid_decode(&l, HOST_RID_KEYBOARD, kb, sizeof(kb), &in) && memcmp(in.keys, keys, 32) == 0, "kb round trip");

    /* System Control as variable bits (our own ID4): Sleep */
    uint8_t sys = 0x02;
    CHECK(hid_decode(&l, HOST_RID_SYSTEM, &sys, 1, &in) && in.has_system && in.system == 0x02, "system round trip");

    /* 7 or more keys: all 6 slots are ErrorRollOver, modifiers unchanged */
    uint8_t many[32] = {0};
    for (int u = 0x04; u < 0x04 + 7; u++) {
        many[u >> 3] |= 1 << (u & 7);
    }
    many[0xE0 >> 3] |= 1;
    host_kb_report(many, kb);
    CHECK(kb[0] == 0x01 && kb[2] == 0x01 && kb[7] == 0x01, "rollover");
    /* 0x00-0x03 and 0xE8 and above are never sent as keys */
    uint8_t odd[32] = {0};
    odd[0] = 0x0E;           /* 0x01~0x03 */
    odd[0xE8 >> 3] |= 1;     /* 0xE8 */
    host_kb_report(odd, kb);
    CHECK(kb[2] == 0, "status usages are not keys");

    /* Mouse: X 40000 splits into 32767 then 7233. Wheel 200 into 127 then 73 */
    host_motion_t m = {0};
    host_motion_add(&m, 40000, -5, 200, -1);
    uint8_t ms[HOST_MOUSE_LEN];
    host_mouse_report(&m, 0x01, ms);
    CHECK(hid_decode(&l, HOST_RID_MOUSE, ms, sizeof(ms), &in) && in.dx == 32767 && in.dy == -5 && in.wheel == 127 &&
          in.pan == -1 && in.buttons == 1, "mouse first chunk dx=%d w=%d", (int)in.dx, (int)in.wheel);
    CHECK(!host_motion_empty(&m), "rest kept");
    host_mouse_report(&m, 0x01, ms);
    CHECK(hid_decode(&l, HOST_RID_MOUSE, ms, sizeof(ms), &in) && in.dx == 7233 && in.wheel == 73 && host_motion_empty(&m),
          "mouse second chunk dx=%d", (int)in.dx);

    /* Boot mouse: ±127, wheel is dropped */
    host_motion_add(&m, -300, 10, 5, 0);
    uint8_t bm[HOST_BOOT_MOUSE_LEN];
    host_boot_mouse_report(&m, 0x0F, bm);
    CHECK(bm[0] == 0x07 && (int8_t)bm[1] == -127 && (int8_t)bm[2] == 10 && m.dx == -173 && m.wheel == 0, "boot mouse");

    /* Consumer: Volume Up */
    uint16_t cu[2] = {0xE9, 0};
    uint8_t cs[HOST_CONSUMER_LEN];
    host_consumer_report(cu, 1, cs);
    CHECK(hid_decode(&l, HOST_RID_CONSUMER, cs, sizeof(cs), &in) && in.consumer_count == 1 && in.consumer[0] == 0xE9,
          "consumer round trip");

}

int main(void)
{
    load_layouts();
    test_input();
    test_combo_parse();
    test_router();
    test_host_report();
    printf("%s (%d passed, %d failed)\n", s_fail ? "FAILED" : "ALL PASSED", s_pass, s_fail);
    return s_fail ? 1 : 0;
}
