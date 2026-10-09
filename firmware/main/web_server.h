#pragma once

#include "esp_err.h"

/* 설정 웹 화면과 API를 켠다. net_mgr_start() 뒤에 호출한다 */
esp_err_t web_server_start(void);
