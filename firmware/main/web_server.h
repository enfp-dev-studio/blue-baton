#pragma once

#include "esp_err.h"

/* Start the settings web UI and API. Call after net_mgr_start() */
esp_err_t web_server_start(void);
