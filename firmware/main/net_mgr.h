#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool sta_configured;   /* 집 Wi-Fi 정보가 저장돼 있는지 */
    bool sta_connected;    /* 지금 집 Wi-Fi에 붙어 있는지 */
    char sta_ssid[33];
    char sta_ip[16];
    char ap_ssid[33];      /* 보드가 직접 여는 Wi-Fi 이름 */
} net_status_t;

/* Wi-Fi를 AP+STA 동시 모드로 켠다. 집 Wi-Fi 정보가 있으면 바로 연결을 시도한다 */
esp_err_t net_mgr_start(void);

/* 집 Wi-Fi 정보를 적용해서 연결한다. 저장은 호출하는 쪽에서 한다 */
esp_err_t net_mgr_wifi_apply(const char *ssid, const char *pass);
void net_mgr_wifi_clear(void);

/* 저장된 보드 Wi-Fi 비밀번호를 다시 적용한다 */
void net_mgr_ap_apply(void);

void net_mgr_get_status(net_status_t *out);
