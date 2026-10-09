set(KM_SRCS "main.c" "cfg_store.c" "json_mini.c" "usb_ctl.c" "ble_pair.c" "hid_map.c" "hid_input.c" "router.c" "host_report.c" "host.c" "gap_name.c")

# Wi-Fi setup is built only when KM_WIFI_ENABLE=y (off by default)
if(CONFIG_KM_WIFI_ENABLE)
    list(APPEND KM_SRCS "net_mgr.c" "web_server.c")
endif()

idf_component_register(SRCS ${KM_SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash bt esp_wifi esp_netif esp_event esp_http_server esp_timer esp_hw_support)
