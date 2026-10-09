#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    bool sta_configured;   /* Whether home Wi-Fi credentials are stored */
    bool sta_connected;    /* Whether currently connected to home Wi-Fi */
    char sta_ssid[33];
    char sta_ip[16];
    char ap_ssid[33];      /* Name of the Wi-Fi network the board hosts itself */
} net_status_t;

/* Start Wi-Fi in concurrent AP+STA mode. If home Wi-Fi credentials exist, try to connect right away */
esp_err_t net_mgr_start(void);

/* Apply home Wi-Fi credentials and connect. The caller does the saving */
esp_err_t net_mgr_wifi_apply(const char *ssid, const char *pass);
void net_mgr_wifi_clear(void);

/* Reapply the stored board Wi-Fi password */
void net_mgr_ap_apply(void);

void net_mgr_get_status(net_status_t *out);
