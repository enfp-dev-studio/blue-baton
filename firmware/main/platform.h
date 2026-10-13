/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

/*
 * What the firmware needs from the chip and its SDK, so the rest (report decoding, routing, the input and host sides
 * on NimBLE) does not depend on ESP-IDF. NimBLE's own porting layer (ble_npl_*) already covers events, tick timers,
 * locks and critical sections on every NimBLE port; this adds what it does not.
 *
 * Error codes (esp_err_t, ESP_OK, ESP_ERR_*) and logging (ESP_LOGx) are the ESP-IDF names; another platform provides
 * the same names. Implementations: platform_esp.c (ESP32-S3) here, settings storage in cfg_store.c (NVS).
 */

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

/* Microseconds since boot, monotonic */
int64_t plat_now_us(void);

/* Free heap now, and the lowest it has been since boot (bytes) */
uint32_t plat_heap_free(void);
uint32_t plat_heap_min(void);

/* Bring up the NimBLE port (controller and host) before ble_hs_cfg is filled in */
esp_err_t plat_ble_init(void);
/* Then the bond store and the NimBLE host task */
void plat_ble_start(void);

/*
 * A periodic timer with a microsecond period (NimBLE callouts run on the 10 ms tick). fn runs in a timer context:
 * it should only post an event to the NimBLE queue.
 */
typedef struct plat_timer plat_timer_t;
plat_timer_t *plat_timer_create(void (*fn)(void *arg), void *arg);
void plat_timer_start_periodic(plat_timer_t *t, uint64_t period_us);
void plat_timer_stop(plat_timer_t *t);

/* The control channel (USB serial): every incoming line goes to on_line, called from the channel's own task */
esp_err_t plat_ctl_start(void (*on_line)(const char *line));
/* Send one line: prefix, then text, in one write so lines from different tasks do not mix */
void plat_ctl_write(const char *prefix, const char *text);
#if CONFIG_KM_DIAG
/* Developer builds: lines sent, and the total and longest time inside plat_ctl_write since the last call, then reset */
void plat_ctl_diag_take(uint32_t *lines, uint32_t *total_us, uint32_t *max_us);
#endif
