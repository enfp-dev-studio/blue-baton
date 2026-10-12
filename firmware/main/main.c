#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_pair.h"
#include "cfg_store.h"
#include "usb_ctl.h"

static const char *TAG = "km";

void app_main(void)
{
    /*
     * Switch combos, pairing records (km_cfg) and NimBLE bond keys (nimble_bond) live in NVS,
     * so open it before BLE. If there are no free pages or the format changed, erase and reopen as the ESP-IDF examples do.
     */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(cfg_init());
    ESP_ERROR_CHECK(ble_pair_init(usb_ctl_emit));
    ESP_ERROR_CHECK(usb_ctl_start());   /* After BLE init: commands are posted to the NimBLE event queue */
}
