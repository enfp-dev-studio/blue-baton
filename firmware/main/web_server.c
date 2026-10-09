#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"

#include "cfg_store.h"
#include "json_mini.h"
#include "net_mgr.h"
#include "web_server.h"

/*
 * Wi-Fi setup API (built only when KM_WIFI_ENABLE=y).
 * USB comes first for now, so no UI (HTML) is attached. It will be added with the same command format when
 * switching to the server mode.
 */

static const char *TAG = "web";

#define PIN_HEADER "X-PIN"
#define BODY_MAX 512
#define RESP_MAX 512

static bool authorized(httpd_req_t *req)
{
    if (!cfg_pin_is_set()) {
        return false;
    }
    char pin[CFG_PIN_MAX + 1] = {0};
    if (httpd_req_get_hdr_value_str(req, PIN_HEADER, pin, sizeof(pin)) != ESP_OK) {
        return false;
    }
    return cfg_pin_check(pin);
}

/* Read the whole request body. On failure buf becomes an empty string */
static void read_body(httpd_req_t *req, char *buf, size_t size)
{
    buf[0] = '\0';
    if (req->content_len <= 0 || (size_t)req->content_len >= size) {
        return;
    }
    int received = 0;
    while (received < req->content_len) {
        int n = httpd_req_recv(req, buf + received, req->content_len - received);
        if (n <= 0) {
            buf[0] = '\0';
            return;
        }
        received += n;
    }
    buf[received] = '\0';
}

static esp_err_t send_json_text(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t send_error(httpd_req_t *req, const char *status, const char *message)
{
    char buf[RESP_MAX];
    snprintf(buf, sizeof(buf), "{\"error\":\"%s\"}", message);
    return send_json_text(req, status, buf);
}

static esp_err_t send_ok(httpd_req_t *req)
{
    return send_json_text(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t status_get(httpd_req_t *req)
{
    bool ok = authorized(req);
    char buf[RESP_MAX];
    int len = snprintf(buf, sizeof(buf), "{\"pin_set\":%s,\"unlocked\":%s",
                       cfg_pin_is_set() ? "true" : "false", ok ? "true" : "false");
    if (ok) {
        net_status_t st;
        net_mgr_get_status(&st);
        char ssid[80];
        char ap[80];
        json_escape(st.sta_ssid, ssid, sizeof(ssid));
        json_escape(st.ap_ssid, ap, sizeof(ap));
        len += snprintf(buf + len, sizeof(buf) - len,
                        ",\"home_wifi_configured\":%s,\"home_wifi_connected\":%s,"
                        "\"home_ssid\":\"%s\",\"home_ip\":\"%s\",\"ap_ssid\":\"%s\"",
                        st.sta_configured ? "true" : "false",
                        st.sta_connected ? "true" : "false",
                        ssid, st.sta_ip, ap);
    }
    snprintf(buf + len, sizeof(buf) - len, "}");
    return send_json_text(req, "200 OK", buf);
}

static esp_err_t unlock_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    read_body(req, body, sizeof(body));
    char pin[CFG_PIN_MAX + 1];
    bool ok = json_get_string(body, "pin", pin, sizeof(pin)) &&
              cfg_pin_is_set() && cfg_pin_check(pin);
    if (!ok) {
        return send_error(req, "401 Unauthorized", "Wrong PIN");
    }
    return send_ok(req);
}

static esp_err_t pin_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    read_body(req, body, sizeof(body));
    if (cfg_pin_is_set() && !authorized(req)) {
        return send_error(req, "401 Unauthorized", "The current PIN is required");
    }
    char pin[CFG_PIN_MAX + 1];
    if (!json_get_string(body, "pin", pin, sizeof(pin)) || strlen(pin) < 4) {
        return send_error(req, "400 Bad Request", "PIN must be 4 to 8 digits");
    }
    if (cfg_pin_set(pin) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Failed to save the PIN");
    }
    return send_ok(req);
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    read_body(req, body, sizeof(body));
    if (!authorized(req)) {
        return send_error(req, "401 Unauthorized", "Enter the PIN first");
    }
    char ssid[CFG_WIFI_SSID_MAX + 1];
    char pass[CFG_WIFI_PASS_MAX + 1] = {0};
    bool has_ssid = json_get_string(body, "ssid", ssid, sizeof(ssid));
    /* The password key is required, but its value may be empty (open Wi-Fi) */
    bool has_pass = json_get_string(body, "password", pass, sizeof(pass));
    size_t pass_len = strlen(pass);
    bool pass_ok = pass_len == 0 || pass_len >= 8;
    if (!has_ssid || ssid[0] == '\0' || !has_pass || !pass_ok) {
        return send_error(req, "400 Bad Request",
                          "Wi-Fi name must be 1 to 32 characters; password 8 to 64 characters or empty");
    }
    esp_err_t err = cfg_wifi_set(ssid, pass);
    if (err == ESP_OK) {
        err = net_mgr_wifi_apply(ssid, pass);
    }
    if (err != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Failed to save the Wi-Fi settings");
    }
    return send_ok(req);
}

static esp_err_t wifi_clear_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    read_body(req, body, sizeof(body));
    if (!authorized(req)) {
        return send_error(req, "401 Unauthorized", "Enter the PIN first");
    }
    cfg_wifi_clear();
    net_mgr_wifi_clear();
    return send_ok(req);
}

static esp_err_t ap_pass_post(httpd_req_t *req)
{
    char body[BODY_MAX];
    read_body(req, body, sizeof(body));
    if (!authorized(req)) {
        return send_error(req, "401 Unauthorized", "Enter the PIN first");
    }
    char pass[CFG_AP_PASS_MAX + 1];
    if (!json_get_string(body, "password", pass, sizeof(pass)) || strlen(pass) < 8) {
        return send_error(req, "400 Bad Request", "Board Wi-Fi password must be 8 to 63 characters");
    }
    if (cfg_ap_pass_set(pass) != ESP_OK) {
        return send_error(req, "500 Internal Server Error", "Failed to save the password");
    }
    net_mgr_ap_apply();
    return send_ok(req);
}

esp_err_t web_server_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &config), TAG, "httpd start failed");

    const httpd_uri_t routes[] = {
        {.uri = "/api/status",      .method = HTTP_GET,  .handler = status_get},
        {.uri = "/api/unlock",      .method = HTTP_POST, .handler = unlock_post},
        {.uri = "/api/pin",         .method = HTTP_POST, .handler = pin_post},
        {.uri = "/api/wifi",        .method = HTTP_POST, .handler = wifi_post},
        {.uri = "/api/wifi/clear",  .method = HTTP_POST, .handler = wifi_clear_post},
        {.uri = "/api/ap/password", .method = HTTP_POST, .handler = ap_pass_post},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &routes[i]), TAG,
                            "register %s failed", routes[i].uri);
    }

    ESP_LOGI(TAG, "Wi-Fi API on port %d", config.server_port);
    return ESP_OK;
}
