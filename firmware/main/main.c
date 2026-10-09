#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "ble_pair.h"
#include "cfg_store.h"
#include "usb_ctl.h"
#if CONFIG_KM_WIFI_ENABLE
#include "net_mgr.h"
#include "web_server.h"
#endif

static const char *TAG = "km";

static void heartbeat_task(void *param)
{
    while (1) {
        bool scanning;
        bool connected;
        bool hid_ready;
        char peer[18];
        ble_pair_status(&scanning, &connected, &hid_ready, peer, sizeof(peer));
        ESP_LOGI(TAG, "alive scanning=%d connected=%d hid_ready=%d", scanning, connected, hid_ready);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    /*
     * PIN, 전환 키 조합, 페어링 기록(km_cfg)과 NimBLE 본딩 키(nimble_bond)가 NVS에 있으므로
     * BLE보다 먼저 연다. 페이지가 없거나 형식이 바뀐 경우는 ESP-IDF 예제의 표준 처리대로 지우고 다시 연다.
     */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(cfg_init());
    ESP_ERROR_CHECK(usb_ctl_start());
    ESP_ERROR_CHECK(ble_pair_init(usb_ctl_emit));

#if CONFIG_KM_WIFI_ENABLE
    ESP_ERROR_CHECK(net_mgr_start());
    ESP_ERROR_CHECK(web_server_start());
#else
    ESP_LOGI(TAG, "Wi-Fi is off (KM_WIFI_ENABLE=n). USB control only.");
#endif

    xTaskCreate(heartbeat_task, "heartbeat", 2048, NULL, 1, NULL);
}
