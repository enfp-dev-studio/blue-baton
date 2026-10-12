#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

/* Open the settings store (NVS, namespace km_cfg). Call after nvs_flash_init() */
esp_err_t cfg_init(void);

/* Host switch combo list (e.g. "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3"). true if a value is stored */
#define CFG_COMBO_MAX 95
bool cfg_combo_get(char *combo, size_t len);
esp_err_t cfg_combo_set(const char *combo);

/*
 * Active host (from 0), restored at boot; 0 if none is stored. One byte under one key: NVS marks the old entry
 * erased and reclaims its page, so rewriting never grows the store. Write only when the active host changes
 */
int cfg_active_host_get(void);
esp_err_t cfg_active_host_set(int host);

/* Signature of the GATT table the bonded hosts last saw (host.c); 0 if none is stored */
uint32_t cfg_gatt_sig_get(void);
esp_err_t cfg_gatt_sig_set(uint32_t sig);

#if CONFIG_KM_DIAG
/* Developer builds: writes since the last call and the longest one (µs), then reset */
void cfg_diag_take(uint32_t *writes, uint32_t *max_us);
#endif

/*
 * Extra information (metadata) for paired devices. The NimBLE bond store is the source of truth
 * for keys and addresses; this holds only what the bond store lacks (name, kind, last connection order).
 * The key is the identity address (type 0 public, 1 random + 6 bytes). Holds up to the bond count.
 */
#define CFG_PAIRED_MAX CONFIG_BT_NIMBLE_MAX_BONDS
typedef struct {
    uint8_t type;       /* identity address type (0 public, 1 random) */
    uint8_t val[6];     /* NimBLE address bytes (stored in reverse order) */
    char name[24];
    uint8_t kinds;      /* HID_KIND_* bits (determined from the Report Map) */
    uint32_t last_seq;  /* Last connection order. Larger is more recent */
    uint8_t role;       /* CFG_ROLE_*: input device or host (the NimBLE store does not know the role) */
    uint8_t host_index; /* Host number 1-3 for a host, otherwise 0 */
} cfg_paired_t;

#define CFG_ROLE_UNKNOWN 0
#define CFG_ROLE_INPUT 1
#define CFG_ROLE_HOST 2

int cfg_paired_list(cfg_paired_t *out, int max);     /* Returns the number of records */
/* Update if the address (type + val) matches, otherwise add. Skips the write if the content is unchanged */
esp_err_t cfg_paired_put(const cfg_paired_t *rec);
esp_err_t cfg_paired_remove(uint8_t type, const uint8_t val[6]);
esp_err_t cfg_paired_clear(void);
