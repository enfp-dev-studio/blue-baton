#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "cfg_store.h"
#include "net_mgr.h"

#define RETRY_DELAY_US (5 * 1000 * 1000)

static const char *TAG = "net";

static esp_timer_handle_t s_retry_timer;
static bool s_sta_configured;
static bool s_sta_connected;
static char s_sta_ip[16];
static char s_ap_ssid[33];

static void sta_config_apply(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
}

static void ap_config_apply(void)
{
    char pass[CFG_AP_PASS_MAX + 1];
    cfg_ap_pass_get(pass, sizeof(pass));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.ap.ssid, s_ap_ssid, sizeof(cfg.ap.ssid) - 1);
    cfg.ap.ssid_len = strlen(s_ap_ssid);
    strncpy((char *)cfg.ap.password, pass, sizeof(cfg.ap.password) - 1);
    cfg.ap.channel = 1;
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
}

static void retry_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "retrying home wifi");
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        if (s_sta_configured) {
            esp_wifi_connect();
        }
        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        s_sta_connected = false;
        s_sta_ip[0] = '\0';
        if (s_sta_configured && !esp_timer_is_active(s_retry_timer)) {
            ESP_LOGW(TAG, "home wifi lost, retry in 5s");
            esp_timer_start_once(s_retry_timer, RETRY_DELAY_US);
        }
        break;
    case WIFI_EVENT_AP_STACONNECTED:
        ESP_LOGI(TAG, "setup client joined");
        break;
    default:
        break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        esp_ip4addr_ntoa(&event->ip_info.ip, s_sta_ip, sizeof(s_sta_ip));
        s_sta_connected = true;
        ESP_LOGI(TAG, "home wifi connected, ip=%s", s_sta_ip);
    }
}

esp_err_t net_mgr_start(void)
{
    /* AP 이름은 MAC 끝 두 바이트로 만든다. 같은 방에 보드가 여러 개여도 구분된다 */
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "KM-Router-%02X%02X", mac[4], mac[5]);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        ip_event_handler, NULL, NULL));

    const esp_timer_create_args_t timer_args = {
        .callback = retry_timer_cb,
        .name = "wifi_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_retry_timer));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ap_config_apply();

    char ssid[CFG_WIFI_SSID_MAX + 1];
    char pass[CFG_WIFI_PASS_MAX + 1];
    s_sta_configured = cfg_wifi_get(ssid, sizeof(ssid), pass, sizeof(pass));
    if (s_sta_configured) {
        sta_config_apply(ssid, pass);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "setup AP up: %s", s_ap_ssid);
    return ESP_OK;
}

esp_err_t net_mgr_wifi_apply(const char *ssid, const char *pass)
{
    s_sta_configured = true;
    if (esp_timer_is_active(s_retry_timer)) {
        esp_timer_stop(s_retry_timer);
    }
    sta_config_apply(ssid, pass);
    esp_wifi_disconnect();   /* 연결돼 있지 않으면 에러가 나도 괜찮다 */
    return esp_wifi_connect();
}

void net_mgr_wifi_clear(void)
{
    s_sta_configured = false;
    if (esp_timer_is_active(s_retry_timer)) {
        esp_timer_stop(s_retry_timer);
    }
    esp_wifi_disconnect();
    s_sta_connected = false;
    s_sta_ip[0] = '\0';
}

void net_mgr_ap_apply(void)
{
    ap_config_apply();
}

void net_mgr_get_status(net_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->sta_configured = s_sta_configured;
    out->sta_connected = s_sta_connected;
    strncpy(out->sta_ip, s_sta_ip, sizeof(out->sta_ip) - 1);
    strncpy(out->ap_ssid, s_ap_ssid, sizeof(out->ap_ssid) - 1);

    char pass[CFG_WIFI_PASS_MAX + 1];
    cfg_wifi_get(out->sta_ssid, sizeof(out->sta_ssid), pass, sizeof(pass));
}
