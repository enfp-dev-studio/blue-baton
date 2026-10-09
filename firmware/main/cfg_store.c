#include <string.h>

#include "esp_log.h"
#include "nvs.h"

#include "cfg_store.h"

#define NVS_NAMESPACE "km_cfg"
#define KEY_PIN "pin"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"
#define KEY_AP_PASS "ap_pass"
#define KEY_COMBO "combo"

static const char *TAG = "cfg";
static nvs_handle_t s_nvs;

esp_err_t cfg_init(void)
{
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }
    /* Erase leftover v1 records (managed separately from bonds). They have no keys and are unusable */
    if (nvs_erase_key(s_nvs, "paired") == ESP_OK) {
        nvs_commit(s_nvs);
        ESP_LOGW(TAG, "removed v1 paired records (no bond keys behind them)");
    }
    return ESP_OK;
}

/* Read a string. false if missing or longer than the buffer */
static bool get_str(const char *key, char *out, size_t len)
{
    size_t need = 0;
    if (nvs_get_str(s_nvs, key, NULL, &need) != ESP_OK || need == 0 || need > len) {
        return false;
    }
    return nvs_get_str(s_nvs, key, out, &need) == ESP_OK;
}

static esp_err_t set_str(const char *key, const char *value)
{
    esp_err_t err = nvs_set_str(s_nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

bool cfg_pin_is_set(void)
{
    char tmp[CFG_PIN_MAX + 1];
    return get_str(KEY_PIN, tmp, sizeof(tmp));
}

bool cfg_pin_check(const char *pin)
{
    char saved[CFG_PIN_MAX + 1];
    if (!get_str(KEY_PIN, saved, sizeof(saved))) {
        return false;
    }
    return strcmp(saved, pin) == 0;
}

esp_err_t cfg_pin_set(const char *pin)
{
    return set_str(KEY_PIN, pin);
}

bool cfg_wifi_get(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    return get_str(KEY_WIFI_SSID, ssid, ssid_len) && get_str(KEY_WIFI_PASS, pass, pass_len);
}

esp_err_t cfg_wifi_set(const char *ssid, const char *pass)
{
    esp_err_t err = set_str(KEY_WIFI_SSID, ssid);
    if (err == ESP_OK) {
        err = set_str(KEY_WIFI_PASS, pass);
    }
    return err;
}

esp_err_t cfg_wifi_clear(void)
{
    nvs_erase_key(s_nvs, KEY_WIFI_SSID);
    nvs_erase_key(s_nvs, KEY_WIFI_PASS);
    return nvs_commit(s_nvs);
}

void cfg_ap_pass_get(char *pass, size_t len)
{
    if (!get_str(KEY_AP_PASS, pass, len)) {
        strncpy(pass, CFG_DEFAULT_AP_PASS, len - 1);
        pass[len - 1] = '\0';
    }
}

esp_err_t cfg_ap_pass_set(const char *pass)
{
    return set_str(KEY_AP_PASS, pass);
}

bool cfg_combo_get(char *combo, size_t len)
{
    return get_str(KEY_COMBO, combo, len);
}

esp_err_t cfg_combo_set(const char *combo)
{
    return set_str(KEY_COMBO, combo);
}

/*
 * Metadata blob. Bump version when the format changes.
 * On a version mismatch, discard it and start with an empty list. Metadata can be rebuilt from bonds
 * (the boot cleanup step creates empty metadata for each bond, and names are re-read on the next connection),
 * so only names are lost.
 * v1 (key "paired") is an old record kept apart from bonds, so it is erased rather than migrated.
 */
#define KEY_PAIRED_V1 "paired"
#define KEY_PAIRED "paired_meta"
#define PAIRED_VERSION 3

typedef struct {
    uint8_t version;
    uint8_t count;
    cfg_paired_t items[CFG_PAIRED_MAX];
} paired_blob_t;

/* v2 format (no role field). v2 held only input devices, so the role migrates as input */
typedef struct {
    uint8_t type;
    uint8_t val[6];
    char name[24];
    uint8_t kinds;
    uint32_t last_seq;
} paired_v2_t;

typedef struct {
    uint8_t version;
    uint8_t count;
    paired_v2_t items[CFG_PAIRED_MAX];
} paired_blob_v2_t;

static bool paired_load_v2(paired_blob_t *b)
{
    paired_blob_v2_t old;
    size_t len = sizeof(old);
    if (nvs_get_blob(s_nvs, KEY_PAIRED, &old, &len) != ESP_OK || len != sizeof(old) || old.version != 2 ||
        old.count > CFG_PAIRED_MAX) {
        return false;
    }
    memset(b, 0, sizeof(*b));
    b->version = PAIRED_VERSION;
    b->count = old.count;
    for (int i = 0; i < old.count; i++) {
        cfg_paired_t *n = &b->items[i];
        n->type = old.items[i].type;
        memcpy(n->val, old.items[i].val, 6);
        memcpy(n->name, old.items[i].name, sizeof(n->name));
        n->kinds = old.items[i].kinds;
        n->last_seq = old.items[i].last_seq;
        n->role = CFG_ROLE_INPUT;
    }
    return true;
}

static esp_err_t paired_save(const paired_blob_t *b);

static void paired_load(paired_blob_t *b)
{
    size_t len = sizeof(*b);
    if (nvs_get_blob(s_nvs, KEY_PAIRED, b, &len) == ESP_OK && len == sizeof(*b) &&
        b->version == PAIRED_VERSION && b->count <= CFG_PAIRED_MAX) {
        return;
    }
    if (paired_load_v2(b)) {
        paired_save(b);   /* Save the migrated format right away */
        ESP_LOGI(TAG, "paired meta v2 -> v3 (%u records)", b->count);
        return;
    }
    memset(b, 0, sizeof(*b));
    b->version = PAIRED_VERSION;
}

static esp_err_t paired_save(const paired_blob_t *b)
{
    esp_err_t err = nvs_set_blob(s_nvs, KEY_PAIRED, b, sizeof(*b));
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    return err;
}

int cfg_paired_list(cfg_paired_t *out, int max)
{
    paired_blob_t b;
    paired_load(&b);
    int n = b.count < max ? b.count : max;
    memcpy(out, b.items, n * sizeof(cfg_paired_t));
    return n;
}

static bool same_addr(const cfg_paired_t *a, uint8_t type, const uint8_t val[6])
{
    return a->type == type && memcmp(a->val, val, 6) == 0;
}

esp_err_t cfg_paired_put(const cfg_paired_t *rec)
{
    paired_blob_t b;
    paired_load(&b);
    for (int i = 0; i < b.count; i++) {
        if (same_addr(&b.items[i], rec->type, rec->val)) {
            if (memcmp(&b.items[i], rec, sizeof(*rec)) == 0) {
                return ESP_OK;   /* Do not rewrite identical content (flash wear) */
            }
            b.items[i] = *rec;
            return paired_save(&b);
        }
    }
    if (b.count >= CFG_PAIRED_MAX) {
        return ESP_ERR_NO_MEM;
    }
    b.items[b.count++] = *rec;
    return paired_save(&b);
}

esp_err_t cfg_paired_remove(uint8_t type, const uint8_t val[6])
{
    paired_blob_t b;
    paired_load(&b);
    int w = 0;
    for (int i = 0; i < b.count; i++) {
        if (!same_addr(&b.items[i], type, val)) {
            b.items[w++] = b.items[i];
        }
    }
    if (w == b.count) {
        return ESP_ERR_NOT_FOUND;
    }
    b.count = w;
    return paired_save(&b);
}

esp_err_t cfg_paired_clear(void)
{
    nvs_erase_key(s_nvs, KEY_PAIRED_V1);
    esp_err_t err = nvs_erase_key(s_nvs, KEY_PAIRED);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }
    return nvs_commit(s_nvs);
}
