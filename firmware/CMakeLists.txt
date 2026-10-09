cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(blue_baton)

# NimBLE runs one security procedure at a time by default (esp_nimble_cfg.h: BLE_SM_MAX_PROCS 1).
# A host's LTK request that finds no free slot gets no reply (ble_sm.c: ble_sm_ltk_req_rx returns
# ENOMEM without a negative reply), so that host's encryption hangs until the 40 s LL response timeout.
# Real hardware: two Macs reconnecting after a board reset, one waited 40 s.
# Allow one procedure per connection. Set after project() so the default build specs do not override it.
idf_build_set_property(COMPILE_DEFINITIONS "MYNEWT_VAL_BLE_SM_MAX_PROCS=CONFIG_BT_NIMBLE_MAX_CONNECTIONS" APPEND)
# Same for pending connection parameter updates (esp_nimble_cfg.h: BLE_GAP_MAX_PENDING_CONN_PARAM_UPDATE 1):
# with one entry, a second host's request gets ENOMEM while the first is in progress (real hardware: two
# hosts ready at the same moment).
idf_build_set_property(COMPILE_DEFINITIONS "MYNEWT_VAL_BLE_GAP_MAX_PENDING_CONN_PARAM_UPDATE=CONFIG_BT_NIMBLE_MAX_CONNECTIONS" APPEND)
