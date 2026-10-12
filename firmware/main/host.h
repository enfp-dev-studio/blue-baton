/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"
#include "router.h"

/*
 * Output side: the board appears to 3 hosts (Mac, Windows) as a HOGP HID device (keyboard + mouse).
 * - Keeps connections to 3 hosts at once and sends reports only to the active host.
 * - Host pairing is accepted only while the "host pairing window" is open. The board presents itself as a
 *   keyboard (KeyboardOnly), and the 6 digits shown on the host screen are typed on a keyboard attached to the
 *   router (or in the GUI).
 * - All functions are called on the NimBLE host task (ble_pair.c forwards requests).
 */

typedef void (*host_event_fn)(const char *json);

/* Register the GATT services. Call after nimble_port_init, before starting the host task */
esp_err_t host_init(host_event_fn emit);

void host_on_sync(void);
void host_on_reset(void);

/* Send router output to the active host. If switched_from is 0 or more, send release-all to that host first */
void host_send(const router_out_t *out, int switched_from);

/* Handle progress (pk_event) from the router in passkey entry mode */
void host_passkey_event(const router_out_t *out);

/* Open or close the host pairing window. While open, new host connections and pairing are accepted (up to 180 seconds) */
esp_err_t host_pair_window(bool open);
bool host_window_open(void);

/* Passkey entered in the GUI */
esp_err_t host_passkey(uint32_t code);

/* Remove one host (index from 0) */
esp_err_t host_remove(int index);

/* [{host, addr, name, connected, state, active}] */
size_t host_list_json(char *buf, size_t len);

/* Host link intervals and mouse pacing statistics as a JSON object. Resets the statistics. Host task only */
size_t host_stats_json(char *buf, size_t len);

#if CONFIG_KM_DIAG
/* Developer builds: mouse or pen self-test traffic to host (from 0) for minutes; host < 0 stops it. Host task only */
esp_err_t host_selftest(int host, int minutes, bool pen);
#endif

/* Deleting a bond (ble_gap_unpair) returns EBUSY while advertising. Stop advertising during the delete, then restore it */
void host_adv_hold(bool hold);


/* Whether the active host can receive input right now */
bool host_online(int index);
