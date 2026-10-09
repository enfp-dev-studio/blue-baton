/*
 * hid_input.c(입력 정규화)와 router.c(합치기, 전환 키 조합) 단위 시험. 보드 없이 Mac에서 돈다.
 * 실기에서 받은 Report Map을 쓴다 (entries/007).
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

/* ---------- 입력 정규화 ---------- */

static void test_input(void)
{
    hid_input_t in;

    CHECK(!s_mouse.truncated && !s_kb.truncated, "layouts fit");
    CHECK(hid_layout_report_bits(&s_mouse, 1) == 48, "mouse report 1 is 48 bits, got %u", hid_layout_report_bits(&s_mouse, 1));
    CHECK(hid_layout_report_bits(&s_kb, 4) == 120, "kb NKRO report is 120 bits");
    CHECK(hid_layout_report_bits(&s_kb, 3) == 64, "kb 6KRO report is 64 bits");

    /* 실기 마우스: 왼쪽 버튼, X +5, Y -5, 휠 +1 */
    CHECK(decode_hex(&s_mouse, 1, "010500FBFF01", &in), "mouse decodes");
    CHECK(in.buttons == 0x01 && in.dx == 5 && in.dy == -5 && in.wheel == 1, "mouse fields b=%u dx=%d dy=%d w=%d",
          in.buttons, (int)in.dx, (int)in.dy, (int)in.wheel);

    /* 리포트 ID 바이트가 앞에 붙어 온 경우 */
    CHECK(decode_hex(&s_mouse, 1, "01010500FBFF01", &in) && in.dx == 5, "report id prefix stripped");

    /* 키보드 안의 마우스 리포트 (ID 1): 버튼, X/Y/휠 8비트, 가로 휠(AC Pan) */
    CHECK(decode_hex(&s_kb, 1, "0105FB01FF", &in), "kb mouse decodes");
    CHECK(in.buttons == 1 && in.dx == 5 && in.dy == -5 && in.wheel == 1 && in.pan == -1, "kb mouse pan=%d", (int)in.pan);

    /* NKRO 비트맵 (ID 4): 비트 번호 = Usage. 'h'(0x0B)와 Space(0x2C) */
    uint8_t nkro[15] = {0};
    nkro[0x0B >> 3] |= 1 << (0x0B & 7);
    nkro[0x2C >> 3] |= 1 << (0x2C & 7);
    CHECK(hid_decode(&s_kb, 4, nkro, sizeof(nkro), &in), "nkro decodes");
    CHECK(in.has_keys && hid_key_down(in.keys, 0x0B) && hid_key_down(in.keys, 0x2C) && !hid_key_down(in.keys, 0x04),
          "nkro keys");

    /* 6키 배열 (ID 3): LShift + 'a' */
    CHECK(decode_hex(&s_kb, 3, "0200040000000000", &in), "6kro decodes");
    CHECK(hid_key_down(in.keys, 0xE1) && hid_key_down(in.keys, 0x04), "6kro shift+a");

    /* ErrorRollOver(0x01)는 키가 아니다 */
    CHECK(decode_hex(&s_kb, 3, "0000010101010101", &in) && !hid_key_down(in.keys, 0x01), "rollover ignored");

    /* 미디어 키 배열 (ID 2): 볼륨 업 0xE9 */
    CHECK(decode_hex(&s_kb, 2, "E900", &in), "consumer decodes");
    CHECK(in.has_consumer && in.consumer_count == 1 && in.consumer[0] == 0xE9, "volume up");

    /* 모르는 리포트 ID, 짧은 데이터 */
    CHECK(!decode_hex(&s_kb, 9, "00", &in), "unknown report id");
    CHECK(decode_hex(&s_mouse, 1, "01", &in) && in.buttons == 1 && in.dx == 0, "short data reads what exists");

    /* Usage가 Report Count보다 적으면 마지막 Usage가 반복된다 (HID 1.11 6.2.2.8): X 하나에 필드 3개 */
    hid_layout_t l;
    uint8_t m[64];
    size_t n = hex("05010902A10109301581257F750895038106C0", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(decode_hex(&l, 0, "010203", &in) && in.dx == 6, "last usage repeats (dx=%d)", (int)in.dx);

    /* Push/Pop: Report Size 8을 저장했다가 되돌린다 */
    n = hex("05010902A10175089501A47501B409308106C0", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(l.field_count == 1 && l.fields[0].bit_size == 8, "push/pop restores report size");

    /* 깨진 Report Map에서 멈춘다 */
    n = hex("050109", m, sizeof(m));
    hid_layout_parse(m, n, &l);
    CHECK(l.field_count == 0, "broken map");
}

/* ---------- 조합 해석 ---------- */

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

/* ---------- 라우터 ---------- */

/* 키보드 6KRO 리포트(ID 3) 하나를 만들어 slot에 넣는다. mods는 수정 키 비트, keys는 일반 키 Usage들 */
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

    /* 정상 전환: Ctrl, Alt를 누르고 2. 조합 수정 키는 보류되어 이전 호스트에 아무것도 가지 않는다 */
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

    /* 다른 일반 키를 잡고 있으면 전환하지 않는다 */
    setup_router();
    k[0] = KX;
    k[1] = K2;
    press(0, LCTRL | LALT, k, 1, &o, &from);
    press(0, LCTRL | LALT, k, 2, &o, &from);
    CHECK(from == -1 && router_active_host() == 0 && hid_key_down(o.keys, K2), "extra key held: no switch, keys pass");

    /* 수정 키가 하나 더 있으면 다른 조합이다 */
    setup_router();
    k[0] = K2;
    press(0, LCTRL | LALT | LSHIFT, k, 1, &o, &from);
    CHECK(from == -1 && router_active_host() == 0, "ctrl+alt+shift+2 is not ctrl+alt+2");

    /* 오른쪽 수정 키도 같은 묶음이다 */
    setup_router();
    k[0] = K2;
    press(0, RCTRL | RALT, NULL, 0, &o, &from);
    press(0, RCTRL | RALT, k, 1, &o, &from);
    CHECK(from == 0 && router_active_host() == 1, "right ctrl+alt+2 switches");

    /* 지금 쓰는 호스트의 조합: 전환은 없지만 조합은 새지 않는다 */
    setup_router();
    k[0] = K1;
    press(0, LCTRL | LALT, k, 1, &o, &from);
    CHECK(from == -1 && router_active_host() == 0 && out_empty(&o), "combo for active host is swallowed");

    /* 조합 키가 두 장치에 나뉘어 있어도 합친 상태로 본다 (키보드 둘) */
    setup_router();
    press(0, LCTRL | LALT, NULL, 0, &o, &from);
    k[0] = 0x20; /* '3' */
    press(1, 0, k, 1, &o, &from);
    CHECK(from == 0 && router_active_host() == 2, "combo across two keyboards");

    /* 장치가 끊기면 그 장치의 키는 뗀 것으로 본다 */
    setup_router();
    k[0] = KA;
    press(0, 0, k, 1, &o, &from);
    CHECK(hid_key_down(o.keys, KA), "a held");
    router_device_gone(0, &o);
    CHECK(out_empty(&o) && o.keys_changed, "device gone releases its keys");

    /* 마우스: 이동은 그대로, 버튼은 합친다 */
    setup_router();
    hid_input_t in;
    decode_hex(&s_mouse, 1, "010500FBFF00", &in);
    router_input(1, 1, &in, &o, &from);
    CHECK(o.motion && o.dx == 5 && o.dy == -5 && o.buttons == 1 && o.buttons_changed, "mouse passes through");
    decode_hex(&s_mouse, 1, "010000000000", &in);
    router_input(1, 1, &in, &o, &from);
    CHECK(!o.motion && !o.buttons_changed, "no motion, button unchanged");

    /* Ctrl+C: 보류한 Ctrl을 먼저 보내고 C */
    setup_router();
    press(0, LCTRL, NULL, 0, &o, &from);
    k[0] = 0x06; /* 'c' */
    press(0, LCTRL, k, 1, &o, &from);
    CHECK(o.pre_valid && hid_key_down(o.pre_keys, 0xE0) && !hid_key_down(o.pre_keys, 0x06), "pre: ctrl alone first");
    CHECK(hid_key_down(o.keys, 0xE0) && hid_key_down(o.keys, 0x06) && o.keys_changed, "then ctrl+c");
    press(0, 0, NULL, 0, &o, &from);
    CHECK(out_empty(&o) && o.keys_changed && !o.pre_valid, "all released");

    /* Ctrl 탭: 누름과 뗌을 연달아 */
    setup_router();
    press(0, LCTRL, NULL, 0, &o, &from);
    press(0, 0, NULL, 0, &o, &from);
    CHECK(o.pre_valid && hid_key_down(o.pre_keys, 0xE0) && out_empty(&o) && o.keys_changed, "ctrl tap = down then up");

    /* Ctrl을 누른 채 마우스를 움직이면 Ctrl을 먼저 보낸다 (Ctrl+스크롤 확대) */
    setup_router();
    press(0, LCTRL, NULL, 0, &o, &from);
    decode_hex(&s_mouse, 1, "000000000001", &in);
    router_input(1, 1, &in, &o, &from);
    CHECK(o.pre_valid && hid_key_down(o.pre_keys, 0xE0) && hid_key_down(o.keys, 0xE0) && o.motion && o.wheel == 1,
          "mouse wheel releases held ctrl first");

    /* 조합에 없는 수정 키(Shift)는 보류하지 않는다 */
    setup_router();
    press(0, LSHIFT, NULL, 0, &o, &from);
    CHECK(hid_key_down(o.keys, 0xE1) && o.keys_changed && !o.pre_valid, "shift is sent at once");

    /* 일반 키를 누른 상태에서 누른 Ctrl은 바로 보낸다 (조합이 될 수 없다) */
    setup_router();
    k[0] = KX;
    press(0, 0, k, 1, &o, &from);
    press(0, LCTRL, k, 1, &o, &from);
    CHECK(hid_key_down(o.keys, 0xE0) && !o.pre_valid, "ctrl after a normal key is not held");

    /* 패스키 받기: 숫자 6개 + Backspace + Enter. 그동안 아무것도 보내지 않는다 */
    setup_router();
    router_passkey_begin();
    const uint8_t digits[] = {0x1E, 0x1F, 0x20, 0x21, 0x22, 0x59 /* 키패드 1 */, 0x2A /* BS */, 0x23 /* 6 */};
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

    /* LED: 전환하면 새 호스트가 마지막으로 쓴 LED 값이 나온다 */
    setup_router();
    router_set_host_led(1, 0x02); /* 호스트 2의 Caps Lock */
    press(0, LCTRL | LALT, NULL, 0, &o, &from);
    k[0] = K2;
    press(0, LCTRL | LALT, k, 1, &o, &from);
    CHECK(from == 0 && o.led_changed && o.led == 0x02, "led follows the active host");

    /* 전환 때 이전 호스트로 보낼 "모두 뗌" */
    router_release_report(&o);
    CHECK(out_empty(&o) && o.buttons == 0 && o.keys_changed && o.buttons_changed, "release report");
}

/* ---------- 출력 리포트 (호스트 쪽) ---------- */

static void test_host_report(void)
{
    hid_layout_t l;
    hid_layout_parse(HOST_REPORT_MAP, HOST_REPORT_MAP_LEN, &l);
    CHECK(HOST_REPORT_MAP_LEN == 194 && !l.truncated, "host map is 194 bytes and fits (len=%zu)", HOST_REPORT_MAP_LEN);
    CHECK(hid_layout_report_bits(&l, HOST_RID_KEYBOARD) == HOST_KB_LEN * 8, "keyboard report 8 bytes");
    CHECK(hid_layout_report_bits(&l, HOST_RID_MOUSE) == HOST_MOUSE_LEN * 8, "mouse report 7 bytes");
    CHECK(hid_layout_report_bits(&l, HOST_RID_CONSUMER) == HOST_CONSUMER_LEN * 8, "consumer report 4 bytes");
    CHECK(hid_layout_report_bits(&l, HOST_RID_SYSTEM) == HOST_SYSTEM_LEN * 8, "system report 1 byte");

    /* 키보드: Shift + a + LANG1(0x90). 왕복: 만든 리포트를 우리 Report Map으로 다시 풀면 같은 키 */
    uint8_t keys[32] = {0};
    keys[0xE1 >> 3] |= 1 << (0xE1 & 7);
    keys[0x04 >> 3] |= 1 << (0x04 & 7);
    keys[0x90 >> 3] |= 1 << (0x90 & 7);
    uint8_t kb[HOST_KB_LEN];
    host_kb_report(keys, kb);
    CHECK(kb[0] == 0x02 && kb[1] == 0 && kb[2] == 0x04 && kb[3] == 0x90 && kb[4] == 0, "kb bytes");
    hid_input_t in;
    CHECK(hid_decode(&l, HOST_RID_KEYBOARD, kb, sizeof(kb), &in) && memcmp(in.keys, keys, 32) == 0, "kb round trip");

    /* 7개 이상: 6칸 모두 ErrorRollOver, 수정 키는 그대로 */
    uint8_t many[32] = {0};
    for (int u = 0x04; u < 0x04 + 7; u++) {
        many[u >> 3] |= 1 << (u & 7);
    }
    many[0xE0 >> 3] |= 1;
    host_kb_report(many, kb);
    CHECK(kb[0] == 0x01 && kb[2] == 0x01 && kb[7] == 0x01, "rollover");
    /* 0x00~0x03, 0xE8 이상은 키로 보내지 않는다 */
    uint8_t odd[32] = {0};
    odd[0] = 0x0E;           /* 0x01~0x03 */
    odd[0xE8 >> 3] |= 1;     /* 0xE8 */
    host_kb_report(odd, kb);
    CHECK(kb[2] == 0, "status usages are not keys");

    /* 마우스: X 40000은 32767 다음 7233으로 나뉜다. 휠 200은 127 다음 73 */
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

    /* Boot 마우스: ±127, 휠은 버린다 */
    host_motion_add(&m, -300, 10, 5, 0);
    uint8_t bm[HOST_BOOT_MOUSE_LEN];
    host_boot_mouse_report(&m, 0x0F, bm);
    CHECK(bm[0] == 0x07 && (int8_t)bm[1] == -127 && (int8_t)bm[2] == 10 && m.dx == -173 && m.wheel == 0, "boot mouse");

    /* 소비자: 볼륨 업 */
    uint16_t cu[2] = {0xE9, 0};
    uint8_t cs[HOST_CONSUMER_LEN];
    host_consumer_report(cu, 1, cs);
    CHECK(hid_decode(&l, HOST_RID_CONSUMER, cs, sizeof(cs), &in) && in.consumer_count == 1 && in.consumer[0] == 0xE9,
          "consumer round trip");

    /* 같은 값은 다시 보내지 않는다. 뗌은 0이 아닌 리포트에만 */
    host_kb_report(keys, kb);   /* Shift + a + LANG1 */
    host_sent_t sent = {0};
    CHECK(host_sent_update(sent.kb, kb, HOST_KB_LEN), "first send");
    CHECK(!host_sent_update(sent.kb, kb, HOST_KB_LEN), "same value not resent");
    host_release_t r = host_release_needed(&sent);
    CHECK(r.kb && !r.mouse && !r.consumer && !r.system, "release only the keyboard");
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
