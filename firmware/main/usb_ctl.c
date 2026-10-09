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

/* PIN을 맞춰 연 상태. 잠그기 명령이나 재부팅 전까지 유지한다 */
static bool s_unlocked;

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

static void reply_err(int id, const char *message)
{
    reply_err_code(id, "error", message);
}

/* BLE 명령의 실패. 화면은 code를 보고 문구를 고른다 */
static void reply_ble_err(int id, esp_err_t err)
{
    switch (err) {
    case ESP_ERR_INVALID_STATE:
        reply_err_code(id, "busy", "지금은 할 수 없습니다 (BLE 준비 전, 이미 연결됨, 다른 요청 진행 중)");
        break;
    case ESP_ERR_NOT_FOUND:
        reply_err_code(id, "not_found", "그 주소의 장치가 없습니다");
        break;
    case ESP_ERR_NO_MEM:
        reply_err_code(id, "no_slot", "입력 장치 자리가 모두 찼습니다");
        break;
    case ESP_ERR_INVALID_ARG:
        reply_err_code(id, "bad_addr", "주소 형식이 틀렸습니다 (AA:BB:CC:DD:EE:FF)");
        break;
    case ESP_ERR_NOT_ALLOWED:
        reply_err_code(id, "host_window", "호스트 추가 창이 열려 있습니다. 창을 닫은 뒤 입력 장치를 추가하세요");
        break;
    default:
        reply_err_code(id, "failed", "실패했습니다");
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

/* 잠겨 있으면 에러를 보내고 false */
static bool require_unlock(int id)
{
    if (s_unlocked) {
        return true;
    }
    reply_err(id, "잠금 상태입니다. auth 명령으로 PIN을 먼저 입력하세요");
    return false;
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
             ",\"fw\":\"km-router\",\"version\":\"%s\",\"pin_set\":%s,\"unlocked\":%s,"
             "\"combo_set\":%s,\"wifi_feature\":%s",
             KM_FW_VERSION,
             cfg_pin_is_set() ? "true" : "false",
             s_unlocked ? "true" : "false",
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

static void cmd_auth(int id, const char *line)
{
    if (!cfg_pin_is_set()) {
        reply_err(id, "PIN이 아직 없습니다. pin_set으로 먼저 정하세요");
        return;
    }
    char pin[CFG_PIN_MAX + 1];
    if (!json_get_string(line, "pin", pin, sizeof(pin))) {
        reply_err(id, "pin 값이 없습니다");
        return;
    }
    if (!cfg_pin_check(pin)) {
        reply_err(id, "PIN이 맞지 않습니다");
        return;
    }
    s_unlocked = true;
    reply_ok(id, "");
}

static void cmd_pin_set(int id, const char *line)
{
    if (cfg_pin_is_set() && !s_unlocked) {
        reply_err(id, "PIN이 이미 있습니다. 먼저 auth로 잠금을 푸세요");
        return;
    }
    char pin[CFG_PIN_MAX + 1];
    if (!json_get_string(line, "pin", pin, sizeof(pin)) || !is_digits(pin, 4, CFG_PIN_MAX)) {
        reply_err(id, "PIN은 숫자 4~8자리여야 합니다");
        return;
    }
    if (cfg_pin_set(pin) != ESP_OK) {
        reply_err(id, "PIN 저장에 실패했습니다");
        return;
    }
    s_unlocked = true;
    reply_ok(id, "");
}

/* 기본 조합. 저장된 값이 없으면 이것을 쓴다 (ble_pair.c의 DEFAULT_COMBOS와 같다) */
#define DEFAULT_COMBOS "ctrl+alt+1,ctrl+alt+2,ctrl+alt+3"

static void cmd_combo_get(int id)
{
    /* 실제로 적용되는 조합을 알려 준다. 저장된 값이 형식에 맞지 않으면 보드는 기본값을 쓴다 */
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

/* 호스트 순서대로 쉼표로 나눈 조합 목록. 형식을 먼저 검사하고 적용한 뒤 저장한다 */
static void cmd_combo_set(int id, const char *line)
{
    char combo[CFG_COMBO_MAX + 1];
    if (!json_get_string(line, "combo", combo, sizeof(combo)) || combo[0] == '\0') {
        reply_err_code(id, "bad_combo", "combo 값이 없습니다 (예: ctrl+alt+1,ctrl+alt+2,ctrl+alt+3)");
        return;
    }
    if (ble_pair_set_combos(combo) != ESP_OK) {
        reply_err_code(id, "bad_combo",
                       "조합 형식이 틀렸습니다. 수정 키(ctrl, shift, alt, cmd)와 일반 키 하나, 호스트는 쉼표로 3개까지");
        return;
    }
    if (cfg_combo_set(combo) != ESP_OK) {
        reply_err(id, "저장에 실패했습니다");
        return;
    }
    reply_ok(id, "");
}

static void cmd_host_pair(int id, const char *line)
{
    bool on = false;
    if (!json_get_bool(line, "on", &on)) {
        reply_err(id, "on(true 또는 false)이 없습니다");
        return;
    }
    esp_err_t err = ble_pair_host_pair(on);
    if (err == ESP_ERR_NO_MEM) {
        reply_err_code(id, "hosts_full", "호스트 3대가 모두 찼습니다. 하나를 지운 뒤 다시 여세요");
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
        reply_err_code(id, "bad_host", "host는 1~3입니다");
        return;
    }
    esp_err_t err = ble_pair_host_remove(host - 1);
    if (err == ESP_ERR_NOT_FOUND) {
        reply_err_code(id, "not_found", "그 번호에 페어링된 호스트가 없습니다");
        return;
    }
    reply_ble(id, err);
}

static void cmd_host_passkey(int id, const char *line)
{
    char code[8];
    if (!json_get_string(line, "code", code, sizeof(code)) || !is_digits(code, 6, 6)) {
        reply_err_code(id, "bad_code", "패스키는 숫자 6자리입니다");
        return;
    }
    esp_err_t err = ble_pair_host_passkey((uint32_t)strtoul(code, NULL, 10));
    if (err != ESP_OK) {
        reply_err_code(id, "no_request", "지금은 패스키를 받을 호스트가 없습니다");
        return;
    }
    reply_ok(id, "");
}

static void cmd_host_select(int id, const char *line)
{
    int host = 0;
    if (!json_get_int(line, "host", &host) || host < 1 || host > 3) {
        reply_err_code(id, "bad_host", "host는 1~3입니다");
        return;
    }
    reply_ble(id, ble_pair_select_host(host - 1));
}

static void cmd_pair_scan(int id, const char *line)
{
    bool on = false;
    if (!json_get_bool(line, "on", &on)) {
        reply_err(id, "on(true 또는 false)이 없습니다");
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

/* addr를 받는 BLE 명령 (connect, disconnect, paired_remove) */
static void cmd_with_addr(int id, const char *line, esp_err_t (*fn)(const char *))
{
    char addr[18];
    if (!json_get_string(line, "addr", addr, sizeof(addr))) {
        reply_err_code(id, "bad_addr", "addr 값이 없습니다");
        return;
    }
    reply_ble(id, fn(addr));
}

static void cmd_pair_accept(int id, const char *line)
{
    bool accept = false;
    if (!json_get_bool(line, "accept", &accept)) {
        reply_err(id, "accept(true 또는 false)이 없습니다");
        return;
    }
    if (ble_pair_accept(accept) != ESP_OK) {
        reply_err_code(id, "no_request", "지금은 수락할 요청이 없습니다");
        return;
    }
    reply_ok(id, "");
}

static void cmd_passkey(int id, const char *line)
{
    char code[8];
    if (!json_get_string(line, "code", code, sizeof(code)) || !is_digits(code, 6, 6)) {
        reply_err(id, "패스키는 숫자 6자리입니다");
        return;
    }
    if (ble_pair_passkey((uint32_t)strtoul(code, NULL, 10)) != ESP_OK) {
        reply_err_code(id, "no_request", "지금은 패스키를 받을 요청이 없습니다");
        return;
    }
    reply_ok(id, "");
}

static void cmd_paired_list(int id)
{
    /* 본딩 8개 x 항목 약 150바이트 */
    static char list[1300];
    ble_pair_paired_json(list, sizeof(list));
    static char buf[1400];
    snprintf(buf, sizeof(buf), "{\"id\":%d,\"ok\":true,\"paired\":%s}", id, list);
    usb_ctl_emit(buf);
}

static void handle_line(const char *line)
{
    int id = 0;
    json_get_int(line, "id", &id);

    char cmd[24];
    if (!json_get_string(line, "cmd", cmd, sizeof(cmd))) {
        reply_err(id, "cmd가 없습니다");
        return;
    }

    if (strcmp(cmd, "hello") == 0) {
        cmd_hello(id);
    } else if (strcmp(cmd, "status") == 0) {
        cmd_status(id);
    } else if (strcmp(cmd, "auth") == 0) {
        cmd_auth(id, line);
    } else if (strcmp(cmd, "lock") == 0) {
        s_unlocked = false;
        reply_ok(id, "");
    } else if (strcmp(cmd, "pin_set") == 0) {
        cmd_pin_set(id, line);
    } else if (strcmp(cmd, "combo_get") == 0) {
        if (require_unlock(id)) cmd_combo_get(id);
    } else if (strcmp(cmd, "combo_set") == 0) {
        if (require_unlock(id)) cmd_combo_set(id, line);
    } else if (strcmp(cmd, "paired_list") == 0) {
        if (require_unlock(id)) cmd_paired_list(id);
    } else if (strcmp(cmd, "paired_remove") == 0) {
        if (require_unlock(id)) cmd_with_addr(id, line, ble_pair_paired_remove);
    } else if (strcmp(cmd, "paired_clear") == 0) {
        if (require_unlock(id)) reply_ble(id, ble_pair_paired_clear());
    } else if (strcmp(cmd, "host_select") == 0) {
        if (require_unlock(id)) cmd_host_select(id, line);
    } else if (strcmp(cmd, "host_pair") == 0) {
        if (require_unlock(id)) cmd_host_pair(id, line);
    } else if (strcmp(cmd, "host_list") == 0) {
        if (require_unlock(id)) cmd_host_list(id);
    } else if (strcmp(cmd, "host_remove") == 0) {
        if (require_unlock(id)) cmd_host_remove(id, line);
    } else if (strcmp(cmd, "host_passkey") == 0) {
        if (require_unlock(id)) cmd_host_passkey(id, line);
    } else if (strcmp(cmd, "pair_scan") == 0) {
        if (require_unlock(id)) cmd_pair_scan(id, line);
    } else if (strcmp(cmd, "pair_list") == 0) {
        if (require_unlock(id)) cmd_pair_list(id, line);
    } else if (strcmp(cmd, "pair_connect") == 0) {
        if (require_unlock(id)) cmd_with_addr(id, line, ble_pair_connect);
    } else if (strcmp(cmd, "disconnect") == 0) {
        if (require_unlock(id)) cmd_with_addr(id, line, ble_pair_disconnect);
    } else if (strcmp(cmd, "pair_accept") == 0) {
        if (require_unlock(id)) cmd_pair_accept(id, line);
    } else if (strcmp(cmd, "passkey") == 0) {
        if (require_unlock(id)) cmd_passkey(id, line);
    } else if (strcmp(cmd, "pair_cancel") == 0) {
        if (require_unlock(id)) {
            ble_pair_cancel();
            reply_ok(id, "");
        }
    } else {
        reply_err(id, "알 수 없는 명령입니다");
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
