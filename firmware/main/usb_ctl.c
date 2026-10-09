#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ble_pair.h"
#include "cfg_store.h"
#include "json_mini.h"
#include "router.h"
#include "usb_ctl.h"
#include "version.h"

#define LINE_MAX_LEN 256
#define RESP_MAX 1024
#define TASK_STACK 8192

#if CONFIG_KM_WIFI_ENABLE
#define WIFI_FEATURE_JSON "true"
#else
#define WIFI_FEATURE_JSON "false"
#endif

void usb_ctl_emit(const char *json)
{
    printf("KM< %s\n", json);
    fflush(stdout);
}

static void reply_ok(int id, const char *fields)
{
    char buf[RESP_MAX];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":true%s}", id, fields);
    usb_ctl_emit(buf);
}

static void reply_err_code(int id, const char *code, const char *message)
{
    char buf[RESP_MAX];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":false,\"code\":\"%s\",\"error\":\"%s\"}", id, code, message);
    usb_ctl_emit(buf);
}

/* Failure of a BLE command. The UI picks its wording from code */
static void reply_ble_err(int id, esp_err_t err)
{
    switch (err) {
    case ESP_ERR_INVALID_STATE:
        reply_err_code(id, "busy", "Not possible right now (BLE not ready, already connected, or another request in progress)");
        break;
    case ESP_ERR_NOT_FOUND:
        reply_err_code(id, "not_found", "No device with that address");
        break;
    case ESP_ERR_NO_MEM:
        reply_err_code(id, "no_slot", "All input device slots are in use");
        break;
    case ESP_ERR_INVALID_ARG:
        reply_err_code(id, "bad_addr", "Invalid address format (AA:BB:CC:DD:EE:FF)");
        break;
    case ESP_ERR_NOT_ALLOWED:
        reply_err_code(id, "host_window", "The host pairing window is open. Close it before adding an input device");
        break;
    default:
        reply_err_code(id, "failed", "Failed");
        break;
    }
}

static void reply_ble(int id, esp_err_t err)
{
    if (err == ESP_OK) {
        reply_ok(id, "");
    } else {
        reply_ble_err(id, err);
    }
}

static bool is_digits(const char *s, size_t min_len, size_t max_len)
{
    size_t n = strlen(s);
    if (n < min_len || n > max_len) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
    }
    return true;
}

static void cmd_hello(int id)
{
    char combo[CFG_COMBO_MAX + 1];
    bool combo_set = cfg_combo_get(combo, sizeof(combo));

    char fields[256];
    snprintf(fields, sizeof(fields),
             ",\"fw\":\"blue-baton\",\"version\":\"%s\",\"combo_set\":%s,\"wifi_feature\":%s",
             KM_FW_VERSION,
             combo_set ? "true" : "false",
             WIFI_FEATURE_JSON);
    reply_ok(id, fields);
}

static void cmd_status(int id)
{
    bool scanning;
    bool connected;
    bool hid_ready;
    char peer[18];
    ble_pair_status(&scanning, &connected, &hid_ready, peer, sizeof(peer));

    char peers[360];
    ble_pair_peers_json(peers, sizeof(peers));
    char fields[600];
    snprintf(fields, sizeof(fields),
             ",\"scanning\":%s,\"connected\":%s,\"hid_ready\":%s,\"peer\":\"%s\",\"peers\":%s,"
             "\"radio\":\"%s\",\"bonds\":%d,\"bonds_max\":%d,\"active_host\":%d",
             scanning ? "true" : "false",
             connected ? "true" : "false",
             hid_ready ? "true" : "false",
             peer,
             peers,
             ble_pair_radio(),
             ble_pair_bond_count(),
             CONFIG_BT_NIMBLE_MAX_BONDS,
             ble_pair_active_host() + 1);
    reply_ok(id, fields);
}

#if CONFIG_KM_WIFI_ENABLE
/*
 * PIN for the Wi-Fi setup API (web_server.c). USB itself is not locked: the computer the board is plugged into is
 * trusted, as with other USB accessories. Over Wi-Fi anyone in range can reach the board, so that API needs a PIN.
 */
static void cmd_pin_set(int id, const char *line)
{
    char pin[CFG_PIN_MAX + 1];
    if (!json_get_string(line, "pin", pin, sizeof(pin)) || !is_digits(pin, 4, CFG_PIN_MAX)) {
        reply_err_code(id, "bad_pin", "PIN must be 4 to 8 digits");
        return;
    }
    if (cfg_pin_set(pin) != ESP_OK) {
        reply_err_code(id, "failed", "Failed to save the PIN");
        return;
    }
    reply_ok(id, "");
}
#endif

/* Input monitor lease (see ble_pair_monitor). The settings UI renews it while the monitor is shown */
static void cmd_monitor(int id, const char *line)
{
    bool on = false;
    if (!json_get_bool(line, "on", &on)) {
        reply_err_code(id, "bad_request", "on (true or false) is missing");
        return;
    }
    char fields[32];
    snprintf(fields, sizeof(fields), ",\"lease_ms\":%lu", (unsigned long)ble_pair_monitor(on));
    reply_ok(id, fields);
}

/* Default combos. Used when nothing is stored (same as DEFAULT_COMBOS in ble_pair.c) */
#define DEFAULT_COMBOS "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3"

static void cmd_combo_get(int id)
{
    /* Report the combos actually in effect. If the stored value is malformed, the board uses the default */
    char combo[CFG_COMBO_MAX + 1] = {0};
    bool stored = cfg_combo_get(combo, sizeof(combo));
    combo_t parsed[ROUTER_HOST_MAX];
    bool valid = stored && combo_parse_list(combo, parsed);
    char esc[200];
    json_escape(valid ? combo : DEFAULT_COMBOS, esc, sizeof(esc));
    char fields[280];
    snprintf(fields, sizeof(fields), ",\"combo\":\"%s\",\"default\":%s,\"stored_invalid\":%s", esc,
             valid ? "false" : "true", stored && !valid ? "true" : "false");
    reply_ok(id, fields);
}

/* Comma-separated combo list in host order. Validate the format first, apply, then save */
static void cmd_combo_set(int id, const char *line)
{
    char combo[CFG_COMBO_MAX + 1];
    if (!json_get_string(line, "combo", combo, sizeof(combo)) || combo[0] == '\0') {
        reply_err_code(id, "bad_combo", "combo is missing (e.g. ctrl+alt+1,ctrl+alt+2,ctrl+alt+3)");
        return;
    }
    if (ble_pair_set_combos(combo) != ESP_OK) {
        reply_err_code(id, "bad_combo",
                       "Invalid combo. Use modifiers (ctrl, shift, alt, cmd) plus one key; up to 3 hosts, comma-separated");
        return;
    }
    if (cfg_combo_set(combo) != ESP_OK) {
        reply_err_code(id, "failed", "Failed to save");
        return;
    }
    reply_ok(id, "");
}

static void cmd_host_pair(int id, const char *line)
{
    bool on = false;
    if (!json_get_bool(line, "on", &on)) {
        reply_err_code(id, "bad_request", "on (true or false) is missing");
        return;
    }
    esp_err_t err = ble_pair_host_pair(on);
    if (err == ESP_ERR_NO_MEM) {
        reply_err_code(id, "hosts_full", "All 3 host slots are in use. Remove one, then open the window again");
        return;
    }
    reply_ble(id, err);
}

static void cmd_host_list(int id)
{
    static char list[700];
    ble_pair_host_list_json(list, sizeof(list));
    static char buf[760];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":true,\"hosts\":%s}", id, list);
    usb_ctl_emit(buf);
}

static void cmd_host_remove(int id, const char *line)
{
    int host = 0;
    if (!json_get_int(line, "host", &host) || host < 1 || host > 3) {
        reply_err_code(id, "bad_host", "host must be 1 to 3");
        return;
    }
    esp_err_t err = ble_pair_host_remove(host - 1);
    if (err == ESP_ERR_NOT_FOUND) {
        reply_err_code(id, "not_found", "No host is paired at that number");
        return;
    }
    reply_ble(id, err);
}

static void cmd_host_passkey(int id, const char *line)
{
    char code[8];
    if (!json_get_string(line, "code", code, sizeof(code)) || !is_digits(code, 6, 6)) {
        reply_err_code(id, "bad_code", "Passkey must be 6 digits");
        return;
    }
    esp_err_t err = ble_pair_host_passkey((uint32_t)strtoul(code, NULL, 10));
    if (err != ESP_OK) {
        reply_err_code(id, "no_request", "No host is waiting for a passkey");
        return;
    }
    reply_ok(id, "");
}

static void cmd_host_select(int id, const char *line)
{
    int host = 0;
    if (!json_get_int(line, "host", &host) || host < 1 || host > 3) {
        reply_err_code(id, "bad_host", "host must be 1 to 3");
        return;
    }
    reply_ble(id, ble_pair_select_host(host - 1));
}

static void cmd_pair_scan(int id, const char *line)
{
    bool on = false;
    if (!json_get_bool(line, "on", &on)) {
        reply_err_code(id, "bad_request", "on (true or false) is missing");
        return;
    }
    reply_ble(id, ble_pair_scan(on));
}

static void cmd_pair_list(int id, const char *line)
{
    bool all = false;
    json_get_bool(line, "all", &all);
    char list[880];
    ble_pair_list_json(list, sizeof(list), all);
    char fields[920];
    snprintf(fields, sizeof(fields), ",\"devices\":%s", list);
    reply_ok(id, fields);
}

/* BLE commands that take addr (connect, disconnect, paired_remove) */
static void cmd_with_addr(int id, const char *line, esp_err_t (*fn)(const char *))
{
    char addr[18];
    if (!json_get_string(line, "addr", addr, sizeof(addr))) {
        reply_err_code(id, "bad_addr", "addr is missing");
        return;
    }
    reply_ble(id, fn(addr));
}

static void cmd_pair_accept(int id, const char *line)
{
    bool accept = false;
    if (!json_get_bool(line, "accept", &accept)) {
        reply_err_code(id, "bad_request", "accept (true or false) is missing");
        return;
    }
    if (ble_pair_accept(accept) != ESP_OK) {
        reply_err_code(id, "no_request", "No request to accept right now");
        return;
    }
    reply_ok(id, "");
}

static void cmd_passkey(int id, const char *line)
{
    char code[8];
    if (!json_get_string(line, "code", code, sizeof(code)) || !is_digits(code, 6, 6)) {
        reply_err_code(id, "bad_code", "Passkey must be 6 digits");
        return;
    }
    if (ble_pair_passkey((uint32_t)strtoul(code, NULL, 10)) != ESP_OK) {
        reply_err_code(id, "no_request", "No request is waiting for a passkey");
        return;
    }
    reply_ok(id, "");
}

static void cmd_paired_list(int id)
{
    /* 8 bonds x about 150 bytes per entry */
    static char list[1300];
    ble_pair_paired_json(list, sizeof(list));
    static char buf[1400];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":true,\"paired\":%s}", id, list);
    usb_ctl_emit(buf);
}

static void cmd_stats(int id)
{
    char stats[300];
    ble_pair_stats_json(stats, sizeof(stats));
    char buf[360];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":true,\"stats\":%s}", id, stats);
    usb_ctl_emit(buf);
}

static void handle_line(const char *line)
{
    int id = 0;
    json_get_int(line, "id", &id);

    char cmd[24];
    if (!json_get_string(line, "cmd", cmd, sizeof(cmd))) {
        reply_err_code(id, "bad_request", "cmd is missing");
        return;
    }

    if (strcmp(cmd, "hello") == 0) {
        cmd_hello(id);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(id);
#if CONFIG_KM_WIFI_ENABLE
    } else if (strcmp(cmd, "pin_set") == 0) {
        cmd_pin_set(id, line);
#endif
    } else if (strcmp(cmd, "monitor") == 0) {
        cmd_monitor(id, line);
    } else if (strcmp(cmd, "stats") == 0) {
        cmd_stats(id);
    } else if (strcmp(cmd, "combo_get") == 0) {
        cmd_combo_get(id);
    } else if (strcmp(cmd, "combo_set") == 0) {
        cmd_combo_set(id, line);
    } else if (strcmp(cmd, "paired_list") == 0) {
        cmd_paired_list(id);
    } else if (strcmp(cmd, "paired_remove") == 0) {
        cmd_with_addr(id, line, ble_pair_paired_remove);
    } else if (strcmp(cmd, "paired_clear") == 0) {
        reply_ble(id, ble_pair_paired_clear());
    } else if (strcmp(cmd, "host_select") == 0) {
        cmd_host_select(id, line);
    } else if (strcmp(cmd, "host_pair") == 0) {
        cmd_host_pair(id, line);
    } else if (strcmp(cmd, "host_list") == 0) {
        cmd_host_list(id);
    } else if (strcmp(cmd, "host_remove") == 0) {
        cmd_host_remove(id, line);
    } else if (strcmp(cmd, "host_passkey") == 0) {
        cmd_host_passkey(id, line);
    } else if (strcmp(cmd, "pair_scan") == 0) {
        cmd_pair_scan(id, line);
    } else if (strcmp(cmd, "pair_list") == 0) {
        cmd_pair_list(id, line);
    } else if (strcmp(cmd, "pair_connect") == 0) {
        cmd_with_addr(id, line, ble_pair_connect);
    } else if (strcmp(cmd, "disconnect") == 0) {
        cmd_with_addr(id, line, ble_pair_disconnect);
    } else if (strcmp(cmd, "pair_accept") == 0) {
        cmd_pair_accept(id, line);
    } else if (strcmp(cmd, "passkey") == 0) {
        cmd_passkey(id, line);
    } else if (strcmp(cmd, "pair_cancel") == 0) {
        ble_pair_cancel();
        reply_ok(id, "");
    } else {
        reply_err_code(id, "unknown_cmd", "Unknown command");
    }
}

static void reader_task(void *arg)
{
    char line[LINE_MAX_LEN];
    while (1) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[--n] = '\0';
        }
        if (n > 0) {
            handle_line(line);
        }
    }
}

esp_err_t usb_ctl_start(void)
{
    BaseType_t ok = xTaskCreate(reader_task, "usb_ctl", TASK_STACK, NULL, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
