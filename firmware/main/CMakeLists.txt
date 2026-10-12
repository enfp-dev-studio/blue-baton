idf_component_register(SRCS "main.c" "cfg_store.c" "json_mini.c" "usb_ctl.c" "ble_pair.c" "hid_map.c" "hid_input.c"
                            "router.c" "host_report.c" "host.c" "gap_name.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash bt esp_timer esp_hw_support)

# Report delivery: host.c counts Number Of Completed Packets per connection (and times them) by wrapping NimBLE's HCI event entry
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=ble_hs_hci_evt_process")
