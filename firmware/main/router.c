#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "router.h"

/* Keyboard page modifiers: 0xE0 LCtrl, E1 LShift, E2 LAlt, E3 LGUI, E4 RCtrl, E5 RShift, E6 RAlt, E7 RGUI */
#define USAGE_MOD_FIRST 0xE0
#define USAGE_MOD_LAST 0xE7

typedef struct {
    bool used;
    uint8_t id;
    uint8_t keys[32];
    uint8_t buttons;
    uint16_t consumer[HID_CONSUMER_MAX];
    uint8_t consumer_count;
} report_state_t;

typedef struct {
    report_state_t reports[ROUTER_REPORT_MAX];
} device_state_t;

static device_state_t s_dev[ROUTER_INPUT_MAX];
static combo_t s_combos[ROUTER_HOST_MAX];
static int s_active;
static uint8_t s_suppressed[32];     /* Keys held at the moment of a switch. Not sent until released */
static uint8_t s_held[32];           /* Combo modifiers on hold. Not sent yet */
static uint8_t s_combo_groups;       /* Modifier set used by the configured combos (COMBO_*) */
static bool s_pk_active;
static uint8_t s_pk_digits[6];
static uint8_t s_pk_len;
static uint8_t s_led[ROUTER_HOST_MAX];
static uint8_t s_last_keys[32];      /* Last sent keys (for change detection) */
static uint8_t s_last_buttons;
static uint16_t s_last_consumer[HID_CONSUMER_MAX];
static uint8_t s_last_consumer_count;

/* ---------- Combo parsing ---------- */

typedef struct {
    const char *name;
    uint8_t usage;
} key_name_t;

/* HID Usage Tables chapter 10 (Keyboard/Keypad Page) */
static const key_name_t KEYS[] = {
    {"enter", 0x28}, {"return", 0x28}, {"esc", 0x29}, {"escape", 0x29}, {"backspace", 0x2A}, {"tab", 0x2B},
    {"space", 0x2C}, {"minus", 0x2D}, {"equal", 0x2E}, {"capslock", 0x39}, {"printscreen", 0x46},
    {"scrolllock", 0x47}, {"pause", 0x48}, {"insert", 0x49}, {"home", 0x4A}, {"pageup", 0x4B},
    {"delete", 0x4C}, {"end", 0x4D}, {"pagedown", 0x4E}, {"right", 0x4F}, {"left", 0x50}, {"down", 0x51},
    {"up", 0x52}, {"grave", 0x35}, {"backquote", 0x35},
};

static bool key_usage(const char *tok, uint8_t *usage)
{
    size_t n = strlen(tok);
    if (n == 1 && tok[0] >= 'a' && tok[0] <= 'z') {
        *usage = (uint8_t)(0x04 + tok[0] - 'a');
        return true;
    }
    if (n == 1 && tok[0] >= '1' && tok[0] <= '9') {
        *usage = (uint8_t)(0x1E + tok[0] - '1');
        return true;
    }
    if (n == 1 && tok[0] == '0') {
        *usage = 0x27;
        return true;
    }
    if (tok[0] == 'f' && n >= 2 && n <= 3) {
        int f = atoi(tok + 1);
        if (f >= 1 && f <= 12) {
            *usage = (uint8_t)(0x3A + f - 1);
            return true;
        }
        if (f >= 13 && f <= 24) {
            *usage = (uint8_t)(0x68 + f - 13);
            return true;
        }
    }
    if (n > 2 && tok[0] == '0' && tok[1] == 'x') {
        char *end;
        long v = strtol(tok + 2, &end, 16);
        if (*end == '\0' && v >= 0x04 && v < USAGE_MOD_FIRST) {
            *usage = (uint8_t)v;
            return true;
        }
        return false;
    }
    for (size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if (strcmp(tok, KEYS[i].name) == 0) {
            *usage = KEYS[i].usage;
            return true;
        }
    }
    return false;
}

static uint8_t mod_group(const char *tok)
{
    if (!strcmp(tok, "ctrl") || !strcmp(tok, "control")) {
        return COMBO_CTRL;
    }
    if (!strcmp(tok, "shift")) {
        return COMBO_SHIFT;
    }
    if (!strcmp(tok, "alt") || !strcmp(tok, "option") || !strcmp(tok, "opt")) {
        return COMBO_ALT;
    }
    if (!strcmp(tok, "gui") || !strcmp(tok, "cmd") || !strcmp(tok, "command") || !strcmp(tok, "win") ||
        !strcmp(tok, "meta") || !strcmp(tok, "super")) {
        return COMBO_GUI;
    }
    return 0;
}

bool combo_parse(const char *text, combo_t *out)
{
    memset(out, 0, sizeof(*out));
    char buf[40];
    size_t n = 0;
    for (const char *p = text; *p && n < sizeof(buf) - 1; p++) {
        if (*p != ' ') {
            buf[n++] = (char)tolower((unsigned char)*p);
        }
    }
    buf[n] = '\0';
    if (n == 0) {
        return false;
    }
    bool have_key = false;
    char *save = NULL;
    for (char *tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
        uint8_t m = mod_group(tok);
        if (m) {
            out->mods |= m;
            continue;
        }
        if (have_key || !key_usage(tok, &out->key)) {
            return false;   /* Only one non-modifier key; reject unknown names */
        }
        have_key = true;
    }
    /* A single key without modifiers would steal normal input, so it is rejected */
    out->valid = have_key && out->mods != 0;
    return out->valid;
}

bool combo_parse_list(const char *text, combo_t out[ROUTER_HOST_MAX])
{
    memset(out, 0, sizeof(combo_t) * ROUTER_HOST_MAX);
    char buf[128];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    int host = 0;
    char *p = buf;
    for (;;) {
        char *comma = strchr(p, ',');
        if (comma) {
            *comma = '\0';
        }
        if (host >= ROUTER_HOST_MAX) {
            return false;
        }
        bool blank = true;
        for (char *q = p; *q; q++) {
            if (*q != ' ') {
                blank = false;
            }
        }
        if (!blank && !combo_parse(p, &out[host])) {
            return false;
        }
        host++;
        if (!comma) {
            break;
        }
        p = comma + 1;
    }
    return true;
}

/* ---------- State ---------- */

void router_init(void)
{
    memset(s_dev, 0, sizeof(s_dev));
    memset(s_suppressed, 0, sizeof(s_suppressed));
    memset(s_held, 0, sizeof(s_held));
    memset(s_led, 0, sizeof(s_led));
    s_pk_active = false;
    s_pk_len = 0;
    memset(s_last_keys, 0, sizeof(s_last_keys));
    s_last_buttons = 0;
    s_last_consumer_count = 0;
    s_active = 0;
}

void router_set_combos(const combo_t combos[ROUTER_HOST_MAX])
{
    memcpy(s_combos, combos, sizeof(s_combos));
    s_combo_groups = 0;
    for (int h = 0; h < ROUTER_HOST_MAX; h++) {
        if (s_combos[h].valid) {
            s_combo_groups |= s_combos[h].mods;
        }
    }
}

int router_active_host(void)
{
    return s_active;
}

void router_set_active_host(int host)
{
    if (host >= 0 && host < ROUTER_HOST_MAX) {
        s_active = host;
    }
}

static report_state_t *report_slot(uint8_t slot, uint8_t id)
{
    device_state_t *d = &s_dev[slot];
    report_state_t *free_slot = NULL;
    for (int i = 0; i < ROUTER_REPORT_MAX; i++) {
        if (d->reports[i].used && d->reports[i].id == id) {
            return &d->reports[i];
        }
        if (!d->reports[i].used && !free_slot) {
            free_slot = &d->reports[i];
        }
    }
    if (free_slot) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = true;
        free_slot->id = id;
    }
    return free_slot;
}

/* Union of all devices and all reports */
static void union_state(uint8_t keys[32], uint8_t *buttons, uint16_t consumer[], uint8_t *consumer_count)
{
    memset(keys, 0, 32);
    *buttons = 0;
    *consumer_count = 0;
    for (int s = 0; s < ROUTER_INPUT_MAX; s++) {
        for (int r = 0; r < ROUTER_REPORT_MAX; r++) {
            const report_state_t *rs = &s_dev[s].reports[r];
            if (!rs->used) {
                continue;
            }
            for (int b = 0; b < 32; b++) {
                keys[b] |= rs->keys[b];
            }
            *buttons |= rs->buttons;
            for (int c = 0; c < rs->consumer_count && *consumer_count < HID_CONSUMER_MAX; c++) {
                consumer[(*consumer_count)++] = rs->consumer[c];
            }
        }
    }
}

static uint8_t held_mod_groups(const uint8_t keys[32])
{
    uint8_t m = 0;
    for (uint8_t u = USAGE_MOD_FIRST; u <= USAGE_MOD_LAST; u++) {
        if (hid_key_down(keys, u)) {
            m |= (uint8_t)(1u << ((u - USAGE_MOD_FIRST) & 3));   /* Left/right map to the same group */
        }
    }
    return m;
}

static int count_normal_keys(const uint8_t keys[32])
{
    int n = 0;
    for (int u = 0; u < USAGE_MOD_FIRST; u++) {
        if (hid_key_down(keys, (uint8_t)u)) {
            n++;
        }
    }
    return n;
}

/* Fill in out, excluding blocked keys and flagging what changed */
static void fill_out(const uint8_t keys[32], uint8_t buttons, const uint16_t consumer[], uint8_t consumer_count,
                     router_out_t *out)
{
    for (int b = 0; b < 32; b++) {
        /* Released keys are unblocked. Pressed again, they are sent normally */
        s_suppressed[b] &= keys[b];
        s_held[b] &= keys[b];
        out->keys[b] = keys[b] & (uint8_t)~s_suppressed[b] & (uint8_t)~s_held[b];
    }
    out->buttons = buttons;
    out->consumer_count = consumer_count;
    memcpy(out->consumer, consumer, sizeof(uint16_t) * consumer_count);
    out->keys_changed = memcmp(out->keys, s_last_keys, 32) != 0;
    out->buttons_changed = out->buttons != s_last_buttons;
    out->consumer_changed = consumer_count != s_last_consumer_count ||
                            memcmp(consumer, s_last_consumer, sizeof(uint16_t) * consumer_count) != 0;
    memcpy(s_last_keys, out->keys, 32);
    s_last_buttons = out->buttons;
    memcpy(s_last_consumer, consumer, sizeof(uint16_t) * consumer_count);
    s_last_consumer_count = consumer_count;
}

static bool is_combo_modifier(unsigned u)
{
    if (u < USAGE_MOD_FIRST || u > USAGE_MOD_LAST) {
        return false;
    }
    return (s_combo_groups >> ((u - USAGE_MOD_FIRST) & 3)) & 1;
}

static bool any_bits(const uint8_t b[32])
{
    for (int i = 0; i < 32; i++) {
        if (b[i]) {
            return true;
        }
    }
    return false;
}

/* Passkey entry: looks only at newly pressed keys. Digits (top row 0x1E-0x27, keypad 0x59-0x62), Backspace, Enter, Esc */
static void passkey_feed(const uint8_t before[32], const uint8_t keys[32], router_out_t *out)
{
    for (unsigned u = 0; u < 256; u++) {
        if (!hid_key_down(keys, (uint8_t)u) || hid_key_down(before, (uint8_t)u)) {
            continue;
        }
        int digit = -1;
        if (u >= 0x1E && u <= 0x26) {
            digit = (int)(u - 0x1E + 1);
        } else if (u == 0x27 || u == 0x62) {
            digit = 0;
        } else if (u >= 0x59 && u <= 0x61) {
            digit = (int)(u - 0x59 + 1);
        }
        if (digit >= 0) {
            if (s_pk_len < 6) {
                s_pk_digits[s_pk_len++] = (uint8_t)digit;
            }
            out->pk_event = ROUTER_PK_PROGRESS;
        } else if (u == 0x2A) {
            if (s_pk_len > 0) {
                s_pk_len--;
            }
            out->pk_event = ROUTER_PK_PROGRESS;
        } else if (u == 0x28 || u == 0x58) {
            if (s_pk_len == 6) {
                uint32_t code = 0;
                for (int i = 0; i < 6; i++) {
                    code = code * 10 + s_pk_digits[i];
                }
                out->pk_event = ROUTER_PK_DONE;
                out->pk_code = code;
            }
        } else if (u == 0x29) {
            out->pk_event = ROUTER_PK_CANCEL;
        }
    }
    out->pk_len = s_pk_len;
}

void router_input(uint8_t slot, uint8_t report_key, const hid_input_t *in, router_out_t *out, int *switched_from)
{
    memset(out, 0, sizeof(*out));
    *switched_from = -1;
    if (slot >= ROUTER_INPUT_MAX) {
        return;
    }
    uint8_t before[32];
    uint8_t b_dummy;
    uint16_t c_dummy[HID_CONSUMER_MAX];
    uint8_t cc_dummy;
    union_state(before, &b_dummy, c_dummy, &cc_dummy);

    report_state_t *rs = report_slot(slot, report_key);
    if (rs) {
        /* A report is the "current state" of its own fields. Kinds absent from this report are left untouched */
        if (in->has_keys) {
            memcpy(rs->keys, in->keys, 32);
        }
        if (in->has_buttons) {
            rs->buttons = in->buttons;
        }
        if (in->has_consumer) {
            rs->consumer_count = in->consumer_count;
            memcpy(rs->consumer, in->consumer, sizeof(rs->consumer));
        }
    }

    uint8_t keys[32];
    uint8_t buttons;
    uint16_t consumer[HID_CONSUMER_MAX];
    uint8_t consumer_count;
    union_state(keys, &buttons, consumer, &consumer_count);
    bool motion = in->has_motion && (in->dx || in->dy || in->wheel || in->pan);

    if (s_pk_active) {
        /* During passkey entry: key input is sent to no host. The mouse passes through */
        passkey_feed(before, keys, out);
        uint8_t none[32] = {0};
        fill_out(none, buttons, consumer, consumer_count, out);
        out->dx = in->dx;
        out->dy = in->dy;
        out->wheel = in->wheel;
        out->pan = in->pan;
        out->motion = motion;
        return;
    }

    /* Check whether the newly pressed key completes some host's combo */
    bool triggered = false;
    uint8_t mods = held_mod_groups(keys);
    if (count_normal_keys(keys) == 1) {
        for (int h = 0; h < ROUTER_HOST_MAX; h++) {
            const combo_t *c = &s_combos[h];
            if (c->valid && c->mods == mods && hid_key_down(keys, c->key) && !hid_key_down(before, c->key)) {
                /* Block everything held now until released. Held modifiers get blocked without ever being sent */
                memcpy(s_suppressed, keys, 32);
                memset(s_held, 0, sizeof(s_held));
                triggered = true;
                if (h != s_active) {
                    *switched_from = s_active;
                    s_active = h;
                    out->led_changed = true;
                    out->led = s_led[h];
                }
                break;
            }
        }
    }

    if (!triggered) {
        /* Is this an event that ends the hold: a held modifier was released, or another key, mouse, or media key arrived */
        bool flush = false;
        if (any_bits(s_held)) {
            for (unsigned u = 0; u < 256 && !flush; u++) {
                bool now = hid_key_down(keys, (uint8_t)u);
                bool was = hid_key_down(before, (uint8_t)u);
                if (was && !now && hid_key_down(s_held, (uint8_t)u)) {
                    flush = true;
                } else if (now && !was && !is_combo_modifier(u)) {
                    flush = true;
                }
            }
            if ((in->has_buttons && buttons != s_last_buttons) || motion ||
                (in->has_consumer && (consumer_count != s_last_consumer_count ||
                                      memcmp(consumer, s_last_consumer, sizeof(uint16_t) * consumer_count) != 0))) {
                flush = true;
            }
        }
        if (flush) {
            /* Send the held modifiers first, together with the state just before this event */
            out->pre_valid = true;
            for (int b = 0; b < 32; b++) {
                out->pre_keys[b] = before[b] & (uint8_t)~s_suppressed[b];
            }
            memset(s_held, 0, sizeof(s_held));
            memcpy(s_last_keys, out->pre_keys, 32);
        }
        /* Hold newly pressed combo modifiers. If a key or mouse button is held, it cannot become a combo, so send right away */
        if (count_normal_keys(keys) == 0 && buttons == 0) {
            for (unsigned u = USAGE_MOD_FIRST; u <= USAGE_MOD_LAST; u++) {
                if (hid_key_down(keys, (uint8_t)u) && !hid_key_down(before, (uint8_t)u) && is_combo_modifier(u)) {
                    s_held[u >> 3] |= (uint8_t)(1u << (u & 7));
                }
            }
        }
    }

    fill_out(keys, buttons, consumer, consumer_count, out);
    out->dx = in->dx;
    out->dy = in->dy;
    out->wheel = in->wheel;
    out->pan = in->pan;
    out->motion = motion;
}

void router_device_gone(uint8_t slot, router_out_t *out)
{
    memset(out, 0, sizeof(*out));
    if (slot >= ROUTER_INPUT_MAX) {
        return;
    }
    memset(&s_dev[slot], 0, sizeof(s_dev[slot]));
    uint8_t keys[32];
    uint8_t buttons;
    uint16_t consumer[HID_CONSUMER_MAX];
    uint8_t consumer_count;
    union_state(keys, &buttons, consumer, &consumer_count);
    fill_out(keys, buttons, consumer, consumer_count, out);
}

void router_release_report(router_out_t *out)
{
    memset(out, 0, sizeof(*out));
    out->keys_changed = true;
    out->buttons_changed = true;
    out->consumer_changed = true;
}

void router_passkey_begin(void)
{
    s_pk_active = true;
    s_pk_len = 0;
    memset(s_held, 0, sizeof(s_held));
}

void router_passkey_end(void)
{
    s_pk_active = false;
    s_pk_len = 0;
    /* Keys held when it ends (usually Enter) are not sent until released */
    uint8_t keys[32];
    uint8_t buttons;
    uint16_t consumer[HID_CONSUMER_MAX];
    uint8_t consumer_count;
    union_state(keys, &buttons, consumer, &consumer_count);
    memcpy(s_suppressed, keys, 32);
}

bool router_passkey_active(void)
{
    return s_pk_active;
}

void router_set_host_led(int host, uint8_t led)
{
    if (host >= 0 && host < ROUTER_HOST_MAX) {
        s_led[host] = led;
    }
}

uint8_t router_host_led(int host)
{
    return host >= 0 && host < ROUTER_HOST_MAX ? s_led[host] : 0;
}
