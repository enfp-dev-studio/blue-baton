#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Input device management (BLE HID, board as Central).
 *
 * Rules
 * - The single source of truth for keys and identity addresses is the NimBLE bond store (persisted in NVS).
 * - BLE state is owned by the single NimBLE host task. The functions below may be called from other tasks;
 *   they hand the request to the host task and wait until it is done. Do not call them from the host task.
 * - The radio (scan, connection attempts) and security procedures are handled one at a time by a state machine.
 * - Bonded devices auto-reconnect through the accept list (the method recommended by HOGP 5.2.2).
 */

/* Callback that receives one outgoing event from the board (a JSON string) */
typedef void (*ble_pair_event_fn)(const char *json);

/* Start NimBLE and deliver events to event */
esp_err_t ble_pair_init(ble_pair_event_fn event);

/* Turn scanning for new devices on or off. Once on, it stops by itself after 60 seconds. Auto-reconnect pauses while scanning */
esp_err_t ble_pair_scan(bool on);

/* Found devices list (JSON array). If all is false, HID devices only */
size_t ble_pair_list_json(char *buf, size_t len, bool all);

/* Connected devices list (JSON array) */
size_t ble_pair_peers_json(char *buf, size_t len);

/* Paired devices list (JSON array, based on NimBLE bonds) */
size_t ble_pair_paired_json(char *buf, size_t len);

/* Remove one pairing (bond keys, resolving list, metadata; disconnects if connected) */
esp_err_t ble_pair_paired_remove(const char *addr);

/* Remove all pairings */
esp_err_t ble_pair_paired_clear(void);

/* Connect to addr ("AA:BB:CC:DD:EE:FF"). Must be a found or bonded device. Pairs on first connection */
esp_err_t ble_pair_connect(const char *addr);

/* Disconnect a connected device. The bond is kept, and it is not auto-reconnected until the user connects it again */
esp_err_t ble_pair_disconnect(const char *addr);

/* Accept or reject a numeric comparison (NUMCMP) request */
esp_err_t ble_pair_accept(bool accept);

/* Send a 6-digit code for a passkey entry (INPUT) request */
esp_err_t ble_pair_passkey(uint32_t code);

/* Stop scanning and user connection attempts, and reject any pending pairing input. Already connected devices stay as they are */
void ble_pair_cancel(void);

void ble_pair_status(bool *scanning, bool *connected, bool *hid_ready,
                     char *peer, size_t peer_len);

/* Radio state: "idle", "scan", "connect", "auto", "cancel" */
const char *ble_pair_radio(void);

/* Number of bonds in the NimBLE store. -1 on failure */
int ble_pair_bond_count(void);

/* Host switch combos "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3" (in host order). Empty string means the default. ESP_ERR_INVALID_ARG if malformed */
esp_err_t ble_pair_set_combos(const char *text);

/* Select the active host directly (from 0). The previous host gets release-all */
esp_err_t ble_pair_select_host(int host);

/* Active host (from 0) */
int ble_pair_active_host(void);

/* ---- Hosts (output side, host.c) ---- */

/* Host pairing window (up to 180 seconds). While open, a new host can pair */
esp_err_t ble_pair_host_pair(bool open);

/* Paired hosts list (JSON array) */
size_t ble_pair_host_list_json(char *buf, size_t len);

/* Remove one host (index from 0) */
esp_err_t ble_pair_host_remove(int index);

/* Passkey shown on the host screen, entered from the GUI */
esp_err_t ble_pair_host_passkey(uint32_t code);
