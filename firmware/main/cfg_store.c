#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "cfg_store.h"

#define NVS_NAMESPACE "km_cfg"
#define KEY_COMBO "combo"
#define KEY_ACTIVE_HOST "active_host"
#define KEY_GATT_SIG "gatt_sig"

static const char *TAG = "cfg";
static nvs_handle_t s_nvs;

/*
 * Developer builds: how long writes take. A flash write stops code running from flash on both cores, so a write on
 * the NimBLE host task holds up reports for that long; this measures it before anything is moved.
 */
#if CONFIG_KM_DIAG
static portMUX_TYPE s_diag_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_diag_writes;
static uint32_t s_diag_max_us;

static void write_done(int64_t t0)
{
    uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
    taskENTER_CRITICAL(&s_diag_lock);
    s_diag_writes++;
    if (us > s_diag_max_us) {
        s_diag_max_us = us;
    }
    taskEXIT_CRITICAL(&s_diag_lock);
}

void cfg_diag_take(uint32_t *writes, uint32_t *max_us)
{
    taskENTER_CRITICAL(&s_diag_lock);
    *writes = s_diag_writes;
    *max_us = s_diag_max_us;
    s_diag_writes = 0;
    s_diag_max_us = 0;
    taskEXIT_CRITICAL(&s_diag_lock);
}
#else
static inline void write_done(int64_t t0) { (void)t0; }
#endif

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
    /* Earlier firmware stored a settings PIN, Wi-Fi credentials and an AP password. Nothing uses them now */
    static const char *const old_keys[] = {"pin", "wifi_ssid", "wifi_pass", "ap_pass"};
    bool erased = false;
    for (size_t i = 0; i < sizeof(old_keys) / sizeof(old_keys[0]); i++) {
        erased |= nvs_erase_key(s_nvs, old_keys[i]) == ESP_OK;
    }
    if (erased) {
        nvs_commit(s_nvs);
        ESP_LOGW(TAG, "removed the old PIN and Wi-Fi settings");
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
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = nvs_set_str(s_nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    write_done(t0);
    return err;
}

bool cfg_combo_get(char *combo, size_t len)
{
    return get_str(KEY_COMBO, combo, len);
}

esp_err_t cfg_combo_set(const char *combo)
{
    return set_str(KEY_COMBO, combo);
}

int cfg_active_host_get(void)
{
    uint8_t v = 0;
    return nvs_get_u8(s_nvs, KEY_ACTIVE_HOST, &v) == ESP_OK ? v : 0;
}

esp_err_t cfg_active_host_set(int host)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = nvs_set_u8(s_nvs, KEY_ACTIVE_HOST, (uint8_t)host);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    write_done(t0);
    return err;
}

uint32_t cfg_gatt_sig_get(void)
{
    uint32_t sig = 0;
    nvs_get_u32(s_nvs, KEY_GATT_SIG, &sig);
    return sig;
}

esp_err_t cfg_gatt_sig_set(uint32_t sig)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = nvs_set_u32(s_nvs, KEY_GATT_SIG, sig);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    write_done(t0);
    return err;
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
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = nvs_set_blob(s_nvs, KEY_PAIRED, b, sizeof(*b));
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    write_done(t0);
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
