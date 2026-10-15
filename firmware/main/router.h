/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hid_input.h"

/*
 * Input router: merges the common form (hid_input_t) of all input devices into "the state to send to the
 * active host now", and intercepts switch combos. It works independently of the output (host-side BLE),
 * so it is tested without the board.
 *
 * Rules (same as typical KVM behavior)
 * - At the moment the last key of a combo is pressed, switch if the held modifier set (Ctrl, Shift, Alt, GUI) exactly
 *   matches the combo and no other key is held. Left/right are not distinguished.
 * - Keys held at that moment are sent to no host until each is released (the combo does not leak, and modifiers
 *   are not carried over to the new host while held).
 * - On switch, the previous host gets a "release-all" state (so no key stays stuck).
 * - When an input device disconnects, its keys and buttons are treated as released.
 * - Modifiers used in a combo are not sent on press; the next event decides (hold). If the combo completes,
 *   nothing is sent and the switch happens. If another key, mouse, or media key arrives, or a held modifier is
 *   released, the held modifiers are sent first (pre_keys) and then that event. So Ctrl+C works as usual, and a
 *   modifier tap goes out as press and release.
 *   This keeps the previous host from seeing the combo's modifiers pressed and released.
 * - Passkey entry mode (host pairing): key input is sent to no host and digits are collected.
 */

#define ROUTER_HOST_MAX 3
#define ROUTER_INPUT_MAX 2
#define ROUTER_REPORT_MAX 16     /* Per-input-report (characteristic) state of one device. IDs can collide across HID services, so keyed by characteristic number */

#define COMBO_CTRL 0x01
#define COMBO_SHIFT 0x02
#define COMBO_ALT 0x04
#define COMBO_GUI 0x08

typedef struct {
    bool valid;
    uint8_t mods;   /* Set of COMBO_* */
    uint8_t key;    /* Keyboard page Usage (not a modifier) */
} combo_t;

/* Format "ctrl+alt+1", "cmd+shift+f1", "ctrl+alt+0x3a". Case-insensitive. false on failure */
bool combo_parse(const char *text, combo_t *out);

/* Used when no combos are stored */
#define ROUTER_DEFAULT_COMBOS "alt+1,alt+2,alt+3"

/* Per-host combo list "alt+1,alt+2,alt+3". An empty entry means no combo for that host. false on failure */
bool combo_parse_list(const char *text, combo_t out[ROUTER_HOST_MAX]);

/* State to send to the active host */
typedef struct {
    uint8_t keys[32];                    /* Pressed keys (including modifiers, excluding blocked keys) */
    uint8_t buttons;
    int32_t dx, dy, wheel, pan;          /* Motion of this input (not accumulated) */
    uint16_t consumer[HID_CONSUMER_MAX];
    uint8_t consumer_count;
    uint8_t system;                      /* System Control bitmap (as in hid_input_t) */
    bool keys_changed;
    bool buttons_changed;
    bool consumer_changed;
    bool system_changed;
    bool motion;
    /* When releasing held modifiers: send this key state as a keyboard report before keys */
    bool pre_valid;
    uint8_t pre_keys[32];
    /* LED value of the new active host on switch (written to the input keyboards) */
    bool led_changed;
    uint8_t led;
    /* Progress of passkey entry mode */
    uint8_t pk_event;   /* ROUTER_PK_* */
    uint8_t pk_len;     /* Digits typed so far */
    uint32_t pk_code;   /* 6-digit value when ROUTER_PK_DONE */
} router_out_t;

#define ROUTER_PK_NONE 0
#define ROUTER_PK_PROGRESS 1   /* A digit was typed or erased */
#define ROUTER_PK_DONE 2       /* Enter: 6 digits complete */
#define ROUTER_PK_CANCEL 3     /* Esc */

void router_init(void);
void router_set_combos(const combo_t combos[ROUTER_HOST_MAX]);
/* Whether a host (from 0) can take input now. A switch combo for a host that cannot does nothing (its keys are still
 * swallowed). Without one, every host can */
void router_set_host_ready_fn(bool (*ready)(int host));
int router_active_host(void);           /* From 0 */
void router_set_active_host(int host);

/*
 * Apply one report from input device slot. report_key is the number that tells reports apart within the device
 * (the firmware uses the Report characteristic number; with several HID services, Report IDs can collide).
 * out: state to send to the active host. switched_from: previous host number if a switch happened, otherwise -1.
 * When a switch happens, the previous host must get the "release-all" built by router_release_report().
 */
void router_input(uint8_t slot, uint8_t report_key, const hid_input_t *in, router_out_t *out, int *switched_from);

/* An input device disconnected. Fill in out with the state where what it was pressing is released */
void router_device_gone(uint8_t slot, router_out_t *out);

/* Release-all state (to the previous host on switch) */
void router_release_report(router_out_t *out);

/* Begin/end passkey entry mode. On begin, release-all must be sent to the active host (router_release_report) */
void router_passkey_begin(void);
void router_passkey_end(void);

/* LED output value written by a host (Num, Caps, Scroll, Compose, Kana). Comes out as router_out_t.led on switch */
void router_set_host_led(int host, uint8_t led);
uint8_t router_host_led(int host);

/* Hosts a and b (from 0) trade numbers: the LED state each wrote and the active host go with them. Switch combos stay
 * with the numbers */
void router_swap_hosts(int a, int b);
