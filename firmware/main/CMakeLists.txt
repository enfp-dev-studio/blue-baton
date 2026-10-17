# Chip-specific parts: platform_esp.c (platform.h), cfg_store.c (settings in NVS), main.c (start-up). The rest is portable C on NimBLE
idf_component_register(SRCS "main.c" "platform_esp.c" "cfg_store.c" "json_mini.c" "usb_ctl.c" "ble_pair.c" "hid_map.c"
                            "hid_input.c" "router.c" "host_report.c" "host.c" "gap_name.c" "trace.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash bt esp_timer esp_hw_support esp_ringbuf)

# Report delivery: host.c counts Number Of Completed Packets per connection (and times them) by wrapping NimBLE's HCI event entry
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ble_hs_hci_evt_process")
