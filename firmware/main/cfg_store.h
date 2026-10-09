#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "sdkconfig.h"

#define CFG_PIN_MAX 8
#define CFG_WIFI_SSID_MAX 32
#define CFG_WIFI_PASS_MAX 64
#define CFG_AP_PASS_MAX 63
/* Default board Wi-Fi password. Used for the first connection; can be changed in the web UI */
#define CFG_DEFAULT_AP_PASS "bluebaton1"

/* Open the settings store (NVS, namespace km_cfg). Call after nvs_flash_init() */
esp_err_t cfg_init(void);

/* PIN for the Wi-Fi setup API only (USB is not locked) */
bool cfg_pin_is_set(void);
bool cfg_pin_check(const char *pin);
esp_err_t cfg_pin_set(const char *pin);

/* Home Wi-Fi credentials. true if a value is stored */
bool cfg_wifi_get(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
esp_err_t cfg_wifi_set(const char *ssid, const char *pass);
esp_err_t cfg_wifi_clear(void);

/* Stored board Wi-Fi password, or the default if none */
void cfg_ap_pass_get(char *pass, size_t len);
esp_err_t cfg_ap_pass_set(const char *pass);

/* Host switch combo list (e.g. "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3"). true if a value is stored */
#define CFG_COMBO_MAX 95
bool cfg_combo_get(char *combo, size_t len);
esp_err_t cfg_combo_set(const char *combo);

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
